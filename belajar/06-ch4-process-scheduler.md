# Bab 6 — Chapter 4: Process, Scheduler, dan Multitasking

Tujuan Chapter 4: OS bisa menjalankan **banyak proses** secara bergantian (multitasking preemptive), dengan `exec`, `ps`, `kill`, program latar (`clock`), dan aplikasi eksternal.

File: `process.h/c`, `scheduler.h/c`, `context-switch.s`, bagian IRQ0 di `interrupt.c`, `cmos.h/c`, serta perintah proses di `shell.c`.

---

## 6.1 Konsep: program vs proses vs context

| Istilah | Arti |
|---|---|
| Program | file executable di disk (mis. `/bin/clock`) |
| Proses | program yang sedang berjalan + status lengkapnya |
| Context | seluruh register CPU + `eip` + `eflags` + page directory (CR3) |
| PCB | struktur kernel yang menyimpan metadata + context + memori proses |
| Scheduler | bagian kernel yang memilih proses berikutnya |
| Preemptive | penjadwal bisa menyela proses tanpa izin (lewat IRQ0 timer) |

Dengan **satu CPU**, multitasking = ilusi: tiap 1 ms (IRQ0), kernel menyimpan context proses sekarang dan memuat context proses lain.

---

## 6.2 `process.h` — struktur (bedah field)

```c
#define PROCESS_NAME_LENGTH_MAX      32
#define PROCESS_PAGE_FRAME_COUNT_MAX 512     // batas memori per proses (kit: 8 → dinaikkan)
#define PROCESS_COUNT_MAX            16
#define USER_STACK_PAGE_COUNT        4
#define USER_STACK_TOP               0x400000u
```
- 512 frame = 2 MiB (cukup untuk shell + BSS besar). Kit awal 8 frame terlalu kecil → semua pembuatan proses gagal.
- Stack user 4 halaman (16 KiB) di bawah 0x400000.

```c
struct Context {
    struct CPURegister    cpu;                       // semua register umum+segmen
    uint32_t              eip;                       // lanjut dari instruksi mana
    uint32_t              eflags;                    // flag saat dilanjutkan
    struct PageDirectory *page_directory_virtual_addr; // CR3 proses
};
```
- `cpu` memakai struct yang sama dengan frame interrupt (Bab 3) sehingga mudah disalin dari interrupt; **urutan field harus sama dengan `intsetup.s`/`context-switch.s`** (koreksi `{ebp, esp}`).

```c
typedef enum PROCESS_STATE {
    PROCESS_STATE_UNUSED = 0,   // slot kosong (zero-init)
    PROCESS_STATE_READY,        // siap dijalankan
    PROCESS_STATE_RUNNING,      // sedang berjalan
    PROCESS_STATE_TERMINATED,   // selesai (belum di-free)
} PROCESS_STATE;

struct ProcessMetadata { uint32_t pid; char name[32]; PROCESS_STATE state; };

struct ProcessControlBlock {
    struct ProcessMetadata metadata;
    struct Context         context;
    struct {
        void     *virtual_addr_used[512];
        uint32_t  page_frame_used_count;
    } memory;                    // daftar halaman milik proses untuk dibebaskan
};

struct ProcessManagerState {
    uint32_t active_process_count;
    uint32_t current_process_index;   // PROCESS_COUNT_MAX = "tidak ada"
    uint32_t next_pid;
};
```
- `_process_list[16]` = tabel PCB global.
- Setiap PCB punya page directory sendiri → isolasi alamat.

---

## 6.3 `process.c` — bedah fungsi

```c
struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
struct ProcessManagerState process_manager_state = {
    .active_process_count  = 0,
    .current_process_index = PROCESS_COUNT_MAX,
    .next_pid              = 1,
};
```
- PCB di BSS → otomatis `UNUSED`. `current = 16` menandakan tidak ada proses aktif.

```c
static uint32_t ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
static uint32_t process_generate_new_pid(void) { return process_manager_state.next_pid++; }
static int32_t process_list_get_inactive_index(void) {
    for (i...) if (_process_list[i].metadata.state == PROCESS_STATE_UNUSED) return i;
    return -1;
}
```
- PID naik monoton mulai 1; slot pertama yang `UNUSED` dipakai untuk PCB baru.

### `build_user_stack(context, argc, argv)` — menyiapkan stack cdecl
1. Validasi `1 <= argc <= 16`.
2. `sp = USER_STACK_TOP (0x400000)`.
3. Untuk i dari `argc-1` turun ke 0:
   - hitung panjang argumen (≤128);
   - `sp -= len+1`; salin string + terminator ke `sp`; simpan alamatnya di `arg_addr[i]`.
   - jika `sp` terlalu rendah → gagal (stack jebol).
4. `sp &= ~3` (align 4); tulis `NULL` (sentinel argv).
5. Tulis pointer argv dari belakang: `sp -= 4; *(uint32_t*)sp = arg_addr[i]`.
6. Tulis `argv_pointer = sp`; `sp -= 4; *sp = argv_pointer`; `sp -= 4; *sp = argc`.
7. `context->cpu.stack.esp = sp`.

Hasil stack (lihat dari `esp`):
```
[esp]    argc
[esp+4]  pointer ke argv[0]
[esp+8]  argv[0]
...
[esp+8+4*argc] NULL
... di atasnya string argumen
```
- Inilah kenapa `main(int argc, char **argv)` langsung bekerja tanpa kode tambahan.

### `process_create_internal(request, argc, argv)` — langkah demi langkah
1. **Cek batas**: `active_process_count >= 16` → error 1.
2. **Cek entrypoint**: `request.buf >= 0xC0000000` → error 2 (program harus di paruh user).
3. Validasi nama/parent/argc.
4. **Snapshot nama dan argv ke buffer kernel**:
   - Setelah CR3 diganti ke page directory baru, pointer ke address space lama (shell) tidak valid lagi. Maka string disalin dulu.
5. **Ambil ukuran file dari inode** (`get_file_size`) — jangan percaya `request.buffer_size` dari pemanggil.
6. **Hitung kebutuhan frame**: `code = ceil(file_size/4096)`, `total = code + 4 (stack)`; cek `<= 512` dan `paging_allocate_check(total*4096)`.
7. Ambil slot PCB; **buat page directory baru** (`paging_create_new_page_directory`).
8. Simpan page directory lama; **aktifkan yang baru** (`paging_use_page_directory`) — mulai sini kita bekerja di address space proses baru.
9. **Alokasikan halaman kode** `0x00000000..` sebanyak `code` frame; masing-masing dicatat di `memory.virtual_addr_used`.
10. **Alokasikan halaman stack** di `0x400000-4*4096 .. 0x400000`.
11. **Muat executable**: `request.buf = 0; request.buffer_size = file_size; read(request)` — kernel tetap terpetakan via PDE[0x300] jadi syscall/ISR tetap jalan.
12. **Inisialisasi context**: nolkan; `ds/es/fs/gs = 0x23` (user data ring 3); `eip = 0` (entry `_start` di alamat 0); `eflags = 0x2 | 0x200` (base + interrupt enable); simpan page directory.
13. `build_user_stack(...)`.
14. **Kembalikan CR3 lama** (`paging_use_page_directory(old)`) — supaya pemanggil (shell/kernel) kembali ke address space-nya.
15. Isi metadata: `pid`, `name` (dipotong 31 char), `state = READY`, `page_frame_used_count++`, `active_process_count++`. Return sukses.

**Jalur gagal (`exit_cleanup`)** — rollback rapi:
- bebaskan semua halaman yang sudah dialokasikan (`paging_free_user_page_frame`),
- kembalikan CR3 lama, bebaskan page directory, nolkan PCB, kembalikan kode error.

### Wrapper
```c
int32_t process_create_user_process(request) {
    argv[0] = nama; return process_create_internal(request, 1, argv);
}
int32_t process_create_user_process_args(request, argc, argv) { return process_create_internal(...); }
```
- `exec` memakai versi `_args`; pembuatan init (shell) memakai versi sederhana.

### `process_get_current_running_pcb_pointer()`
- Jika `current_process_index >= 16` → NULL; jika state UNUSED → NULL; jika tidak → `&_process_list[current]`.

### `process_destroy(pid)` — membunuh proses
1. Cari PCB dengan pid & state ≠ UNUSED.
2. Simpan page directory target.
3. `paging_free_user_page_frame` hanya bekerja pada CR3 aktif → jika target bukan proses sekarang, **aktifkan CR3 target** dulu.
4. Bebaskan semua halaman di `memory.virtual_addr_used[]`.
5. Jika target adalah proses sekarang: ganti CR3 ke `_paging_kernel_page_directory` (jangan pernah free page directory yang masih di CR3!), set `current = 16`. Jika bukan: kembalikan CR3 lama.
6. `paging_free_page_directory(target)`, nolkan PCB, kurangi counter. Return true.

---

## 6.4 `scheduler.h/c` — memilih proses berikutnya

```c
#define PIT_MAX_FREQUENCY   1193182   // basis clock PIT
#define PIT_TIMER_FREQUENCY 1000      // 1000 Hz = tiap 1 ms
#define PIT_TIMER_COUNTER   (1193182 / 1000)  // 1193
```
- PIT menghitung mundur dari `divisor`; saat mencapai 0 → IRQ0. Untuk 1 ms: divisor ≈ 1193.

```c
static void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    out(0x43, PIT_COMMAND_VALUE);               // mode: binary, square wave, lobyte/hibyte, channel 0
    out(0x40, counter & 0xFF);                  // kirim divisor low
    out(0x40, (counter >> 8) & 0xFF);           // kirim divisor high
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << 0));  // unmask IRQ0
}
```
- Command `0x36` (hasil komposisi konstanta) = channel 0, akses lobyte/hibyte, mode 3 (square wave), binary.

```c
void scheduler_init(void) {
    process_manager_state.current_process_index = PROCESS_COUNT_MAX;
    activate_timer_interrupt();
}
```
- **Jangan reset `_process_list`** di sini: proses init sudah dibuat sebelum `scheduler_init` dipanggil (lihat `kernel_setup`).

```c
bool scheduler_has_runnable_process(void);   // ada READY/RUNNING?
void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    pcb = process_get_current_running_pcb_pointer();
    if (!pcb) return;
    pcb->context = ctx;                                  // simpan register+eip+eflags+CR3
    if (state == RUNNING) state = READY;                 // siap dipilih lagi nanti
}
```

```c
__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    start = (current + 1) % 16;                          // round-robin mulai dari setelah sekarang
    for (i = 0; i < 16; i++) {
        index = (start + i) % 16;
        pcb = &_process_list[index];
        if (state != READY && state != RUNNING) continue;
        current = index;
        state = RUNNING;
        paging_use_page_directory(pcb->context.page_directory_virtual_addr); // ganti CR3
        process_context_switch(pcb->context);            // tidak kembali
    }
    // tidak ada proses: idle
    current = 16; sti; while (1) hlt;
}
```
- **Round-robin**: mulai dari `current+1`, pilih yang pertama READY/RUNNING.
- `paging_use_page_directory` wajib sebelum switch: alamat stack/context proses baru hanya valid di address space-nya.
- Jika tidak ada proses, CPU di-idle (`hlt`) sampai interrupt berikutnya.
- Fungsi `noreturn` karena berpindah ke proses lain lewat `iret`.

---

## 6.5 `context-switch.s` — bedah baris

```
process_context_switch(struct Context ctx)   // argumen by-value via stack
[esp]   = return address
[esp+4] = ctx (base)
```
Offset field (dari komentar file):
```
+0 edi  +4 esi  +8 ebp  +12 esp(user)  +16 ebx  +20 edx  +24 ecx  +28 eax
+32 gs  +36 fs  +40 es   +44 ds        +48 eip  +52 eflags  +56 page_directory
```
```asm
process_context_switch:
    mov edx, esp                 ; simpan pointer argumen

    ; bangun frame iret (urutan push terbalik)
    push dword 0x23              ; ss user (0x20|3)
    mov eax, [edx + 4 + 12]      ; ctx.cpu.stack.esp (user esp)
    push eax
    mov eax, [edx + 4 + 52]      ; eflags
    push eax
    push dword 0x1B              ; cs user (0x18|3)
    mov eax, [edx + 4 + 48]      ; eip
    push eax
```
- `iret` inter-privilege mengharapkan stack: `eip, cs, eflags, esp, ss`. Karena stack tumbuh ke bawah, kita push dari `ss` dulu sampai `eip` terakhir. Kelimanya membuat CPU berpindah ke ring 3.

```asm
    mov eax, [edx + 4 + 44]      ; ds
    mov ds, ax                   ; ... es, fs, gs
    mov edi, [edx + 4 + 0]
    mov esi, [edx + 4 + 4]
    mov ebp, [edx + 4 + 8]
    mov ebx, [edx + 4 + 16]
    mov ecx, [edx + 4 + 24]
    mov eax, [edx + 4 + 28]
    mov edx, [edx + 4 + 20]      ; edx terakhir (sebelumnya dipakai sebagai base)
    iret
```
- Semua register umum+index dipulihkan persis seperti saat disimpan.
- **`edx` harus paling akhir** karena dipakai sebagai penunjuk context.
- Tidak ada `ret`: `iret` mengembalikan ke user dan menjadi titik "kelahiran"/"kelanjutan" proses.

---

## 6.6 IRQ0 di `interrupt.c` — jantung preemption

```c
case PIC1_OFFSET + IRQ_TIMER: {
    pic_ack(IRQ_TIMER);
    if (process_get_current_running_pcb_pointer() == NULL
        && !scheduler_has_runnable_process()) break;   // idle: jangan switch, jangan rekursi
    struct Context context = {
        .cpu    = frame->cpu,
        .eip    = frame->int_stack.eip,
        .eflags = frame->int_stack.eflags,
        .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
    };
    scheduler_save_context_to_current_running_pcb(context);
    scheduler_switch_to_next_process();                 // tidak kembali
    break;
}
```
- ACK dulu supaya PIC tidak menyandera IRQ lain.
- Snapshot context dari frame interrupt: register dari `cpu`, `eip/eflags` dari `int_stack`.
- **Kasus idle**: bila tidak ada proses, cukup `break` sehingga stub `iret` kembali ke loop `hlt` — menghindari rekursi stack.
- Setelah save, pilih proses berikutnya dan lompat (fungsi `noreturn`).

**Rantai lengkap preemption:**
```
PIT habis → IRQ0 → IDT[0x20] → stub → main_interrupt_handler
  → simpan context proses A ke PCB A (state A = READY)
  → pilih proses B (state = RUNNING), ganti CR3
  → process_context_switch(B) → iret → B jalan di ring 3
```

---

## 6.7 CMOS/RTC (`cmos.h/c`) — untuk perintah `clock`

```c
static uint8_t cmos_read(uint8_t reg) { out(0x70, reg); return in(0x71); }
static bool cmos_is_updating(void) { return (cmos_read(0x0A) & 0x80) != 0; }
static uint8_t bcd_to_binary(uint8_t v) { return (v & 0x0F) + ((v >> 4) * 10); }
```
- Port `0x70` = indeks register, `0x71` = data.
- Register: 0x00 detik, 0x02 menit, 0x04 jam, 0x07 hari, 0x08 bulan, 0x09 tahun, 0x0A status A (bit 7 = sedang update), 0x0B status B (format).
- `cmos_read_rtc` menunggu update selesai, membaca semua, konversi BCD→biner bila perlu (`status_b & 0x04`), menangani mode 12 jam (`0x02`), dan menambahkan 2000 ke tahun.

`clock.c` memakai syscall `get_time` (14) + `write_at` (18) untuk mencetak `[HH:MM:SS]` di pojok kanan bawah tanpa mengganggu output shell.

---

## 6.8 Perintah proses di shell

| Perintah | Implementasi |
|---|---|
| `exec <path>` / `<nama>` | `resolve_executable` (PATH) → syscall `exec` (11) dengan argv; shell lanjut (background) |
| `ps` | syscall `ps` (13) → kernel menulis daftar `PID/nama/state` ke buffer |
| `kill <pid>` | syscall `kill` (12); jika membunuh diri sendiri, kernel switch ke proses lain |
| `clock` | program terpisah `/bin/clock`; jalan sebagai proses background |
| `export PATH <dir>` / `env` | bonus (Bab 7) |
| `hello`, `echo` | perintah eksternal di `/bin` (bonus) |
| exit program | `crt0` memanggil syscall 10 setelah `main` selesai → `process_destroy` |

`run_line` juga mendukung `&` (background): perintah dijalankan lewat `exec` dan shell tidak menunggu.

---

## 6.9 `kernel.c` final — urutan boot M4

```c
load_gdt(&_gdt_gdtr);                 // GDT kernel+user+TSS
pic_remap(); initialize_idt();        // IRQ & IDT (termasuk gate syscall 0x30)
activate_keyboard_interrupt();
framebuffer_clear(); framebuffer_set_cursor(0, 0);
initialize_filesystem_ext2();         // EXT2 dari storage.bin
gdt_install_tss(); set_tss_register();// TSS untuk ring 3
set_tss_kernel_current_stack();       // esp0 = stack kernel
process_create_user_process(shell);   // baca /shell, buat page dir + context
scheduler_init();                     // PIT 1000 Hz + unmask IRQ0
scheduler_switch_to_next_process();   // iret pertama ke ring 3
while (true) hlt;
```
- Jika `process_create_user_process` gagal, kernel mencetak kode error dan berhenti (`make prepare` untuk mengisi disk).
- Perhatikan: proses init dibuat **sebelum** `scheduler_init` karena komentar di scheduler melarang reset tabel di `scheduler_init`.

---

## 6.10 Tanya-Jawab Demo Ch4

1. **Apa bedanya program, proses, thread?** Program = file; proses = program + address space + PCB; thread = alur eksekusi dalam satu address space (tidak dibuat di sini).
2. **Kenapa context switch harus assembly?** Karena harus mengganti `esp`, `eip`, `cs`, `eflags`, `ss` secara atomik; C tidak bisa (dan nyawa terakhirnya `iret`).
3. **Kenapa ganti CR3 sebelum switch?** Stack user proses tujuan hanya terpetakan di page directory-nya sendiri.
4. **Bagaimana proses tahu kapan harus berhenti?** `crt0` memanggil `exit` (syscall 10) setelah `main`; kernel `process_destroy` lalu memilih proses lain.
5. **Apa yang terjadi jika semua hlt?** CPU idle (`hlt`) sampai IRQ berikutnya; kernel tetap responsif.
6. **Kenapa PIT 1000 Hz?** Periode 1 ms: cukup halus untuk respons, tidak membebani CPU dengan overhead interrupt berlebihan.
7. **Apa isi satu PCB?** metadata (pid/nama/state), context (register+eip+eflags+CR3), daftar halaman memori.
8. **Kenapa `exec` bersifat background di sini?** Shell tidak memblokir; proses anak dijadwalkan oleh scheduler. (Tidak ada `wait`/exit-code reaping.)

---

Bab berikutnya: `07-bonus-env-separate-commands.md` — bonus PATH/lingkungan, perintah terpisah, recursive cp/rm, dan Shift keyboard.
