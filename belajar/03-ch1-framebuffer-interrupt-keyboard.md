# Bab 3 — Chapter 1: Framebuffer, Interrupt, dan Keyboard

Tujuan Chapter 1: OS bisa **menampilkan** sesuatu dan **menerima input** keyboard. Ini fondasi semua interaksi (shell di Bab 5).

Urutan file yang dibedah: `portio.h/c` → `framebuffer.h/c` → `interrupt.h` → `idt.h/c` → `intsetup.s` → `interrupt.c` → `keyboard.h/c`.

---

## 3.1 `portio.h/c` — jembatan ke Port I/O

Hardware x86 punya dua cara diakses: MMIO (alamat memory) dan **PMIO** (nomor port + instruksi `in/out`). Portio adalah pembungkus C untuk instruksi itu.

```c
void out(uint16_t port, uint8_t data) {
    __asm__(
        "outb %0, %1"
        : // <output kosong>
        : "a"(data), "Nd"(port)
    );
}
```
- `outb` = kirim 1 byte ke port.
- `"a"(data)`: nilai `data` ditempatkan di register `al` (huruf a = eax/ax/al).
- `"Nd"(port)`: `Nd` = konstanta 8/16-bit atau register; `outb` menerima nomor port dari `dx` atau immediate.
- Tidak ada `volatile` karena tidak ada nilai kembalian/output; efek sampingnya langsung ke hardware.

```c
uint8_t in(uint16_t port) {
    uint8_t result;
    __asm__ volatile(
        "inb %1, %0"
        : "=a"(result)
        : "Nd"(port)
    );
    return result;
}
```
- `volatile` wajib: hasilnya bukan sekadar perhitungan, tapi pembacaan hardware yang bisa berubah.
- `=a(result)`: hasil `inb` (di `al`) ditulis ke variabel C.

Versi final menambahkan `out16`/`in16` (2 byte) untuk ATA (Bab 4). Pola sama, hanya `outw`/`inw`.

**Demo**: "Kenapa `in` harus `volatile`?" → Karena tanpa itu kompiler boleh menganggap hasilnya tidak berubah dan mengoptimasi pembacaan berulang; padahal hardware bisa mengubah nilainya.

---

## 3.2 `framebuffer.h/c` — menulis ke layar

### Konsep: text mode VGA

Layar text mode 80×25. Setiap sel = **2 byte** di memory mulai alamat fisik `0xB8000`:
- byte genap = kode ASCII karakter;
- byte ganjil = warna: 4 bit bawah = foreground, 4 bit atas = background.

Alamat sel (row, col): `0xB8000 + (row*80 + col) * 2`.

Karena kernel final di higher-half, alamat fisik `0xB8000` sama dengan virtual `0xC00B8000`:
```c
#define FRAMEBUFFER_MEMORY_OFFSET ((uint8_t*) 0xC00B8000)
```
Inilah kenapa Bab 2 menjelaskan paging sebelum Bab 3 di repo final. Di M1 (sebelum paging) nilainya masih `0xB8000`.

### `framebuffer_write` — inti semuanya
```c
void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint32_t index = ((uint32_t) row * FRAMEBUFFER_WIDTH + col) * 2;

    FRAMEBUFFER_MEMORY_OFFSET[index]     = c;
    FRAMEBUFFER_MEMORY_OFFSET[index + 1] = (bg << 4) | (fg & 0x0F);
}
```
Baris demi baris:
- `index`: posisi byte karakter. `row*80 + col` = nomor sel, dikali 2 karena 2 byte/sel.
- `[index] = c`: tulis ASCII.
- `[index+1]`: susun atribut warna. `bg << 4` menaruh background di 4 bit atas; `fg & 0x0F` memastikan foreground hanya 4 bit; `|` menggabungkan.
- Cast `(uint32_t)` sebelum perkalian untuk mencegah overflow bila row/col besar (walau di sini kecil).

### `framebuffer_set_cursor` — kursor via PMIO
```c
uint16_t position = (uint16_t) r * FRAMEBUFFER_WIDTH + c;
out(CURSOR_PORT_CMD,  0x0E);                  // pilih register "high byte" posisi
out(CURSOR_PORT_DATA, position >> 8);         // kirim bit 8-15
out(CURSOR_PORT_CMD,  0x0F);                  // pilih register "low byte"
out(CURSOR_PORT_DATA, position & 0xFF);       // kirim bit 0-7
```
- Kartu VGA punya indeks register: tulis nomor register ke `0x3D4`, lalu nilainya ke `0x3D5`.
- Register `0x0E`/`0x0F` = posisi kursor 16-bit; karena port data 8-bit, dikirim dua kali.

### `framebuffer_clear`
```c
for (i = 0; i < 80*25; i++) {
    [i*2]   = 0x00;   // karakter kosong
    [i*2+1] = 0x07;   // abu-abu di hitam
}
fb_row = 0; fb_col = 0;
framebuffer_set_cursor(0, 0);
```
Loop seluruh 2000 sel, reset posisi kursor yang dilacak.

### Pelacakan kursor & scrolling (versi final)
```c
static uint8_t fb_row = 0, fb_col = 0;
```
Selama ini `framebuffer_write` menulis di posisi eksplisit. Program (shell) butuh "cetak karakter berikutnya" tanpa menyebut posisi → kita lacak posisi terakhir.

`framebuffer_putchar(c, color)`:
- `color & 0x0F` → fg; `color >> 4` → bg.
- `'\n'`: pindah baris, kolom 0.
- `'\r'`: kolom 0.
- `'\b'` (backspace): mundur satu kolom; kalau di awal baris, mundur ke akhir baris sebelumnya; tulis spasi untuk menghapus.
- karakter biasa: tulis lalu `fb_col++`; kalau lewat kolom 79, pindah baris.
- kalau `fb_row >= 25`: **scroll**.

```c
static void framebuffer_scroll(void) {
    uint32_t row_size = 80 * 2;
    memmove(FRAMEBUFFER_MEMORY_OFFSET,
            FRAMEBUFFER_MEMORY_OFFSET + row_size,
            row_size * (25 - 1));
    memset(FRAMEBUFFER_MEMORY_OFFSET + row_size * (25 - 1), 0, row_size);
}
```
- `memmove`: geser semua baris 1..24 ke atas satu baris (baris 0 hilang).
- `memset`: baris terakhir dikosongkan.
- `memmove` (bukan `memcpy`) karena area sumber & tujuan **tumpang tindih**; `memmove` aman untuk overlap.
- `framebuffer_puts(buffer, count, color)`: panggil `putchar` berulang.
- `framebuffer_move_cursor`/`get_cursor`: dipakai program user (mis. `clock`) untuk mencetak di posisi tetap.

**Demo**: "Kenapa `memmove`, bukan `memcpy`?" → Karena blok sumber dan tujuan overlap; `memcpy` tidak menjamin hasil benar saat overlap.

---

## 3.3 `interrupt.h` — struktur data frame interrupt

Saat interrupt terjadi, CPU mendorong beberapa nilai ke stack. Kita perlu struct yang **persis** cocok dengan tata letak itu.

```c
struct CPURegister {
    struct { uint32_t edi, esi; } index;
    struct { uint32_t esp, ebp; } stack;
    struct { uint32_t ebx, edx, ecx, eax; } general;
    struct { uint32_t gs, fs, es, ds; } segment;
} __attribute__((packed));
```
Urutannya mengikuti urutan push di `intsetup.s` (dari alamat rendah ke tinggi). `packed` agar tidak ada padding.

> Catatan penting (dari "correction" Chapter 4): aslinya struct menulis `{esp, ebp}`, tetapi `pushad` x86 menyimpan `... ESP, EBP ...` sehingga urutan yang benar di memory adalah `{ebp, esp}`. Koreksi ini diterapkan agar konteks proses (Bab 6) benar.

```c
struct InterruptStack {  // didorong otomatis oleh CPU
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};
struct InterruptFrame {
    struct CPURegister    cpu;        // hasil pushad + push segmen (kode kita)
    uint32_t              int_number; // didorong stub (kode kita)
    struct InterruptStack int_stack;  // didorong CPU
};
```
Urutan dari bawah ke atas (semakin tinggi alamat): `cpu` → `int_number` → `error_code, eip, cs, eflags`. Pointer yang dikirim ke `main_interrupt_handler` menunjuk awal struct ini.

Konstanta PIC:
- Port: `0x20` (master command), `0x21` (master data), `0xA0/0xA1` (slave).
- `PIC1_OFFSET 0x20`, `PIC2_OFFSET 0x28`: alamat vektor baru setelah remap.
- `PIC_ACK 0x20`: End-Of-Interrupt.
- ICW1..ICW4: nilai konfigurasi PIC.

Versi final menambahkan `struct TSSEntry` (untuk user mode, Bab 5) dan deklarasi `syscall`.

---

## 3.4 `idt.h`/`idt.c` — tabel handler interrupt

Setiap entri IDT = 8 byte, formatnya mirip SegmentDescriptor tetapi untuk handler.

```c
struct IDTGate {
    uint16_t offset_low;      // alamat handler bit 15:0
    uint16_t segment;         // selector code segment (0x8)
    uint8_t  _reserved : 5;   // wajib 0
    uint8_t  _r_bit_1  : 3;   // 0b000 (type gate)
    uint8_t  _r_bit_2  : 3;   // 0b110 (type: interrupt gate)
    uint8_t  gate_32   : 1;   // 1 = gate 32-bit
    uint8_t  _r_bit_3  : 1;   // 0
    uint8_t  dpl       : 2;   // privilege yang boleh memicu
    uint8_t  valid_bit : 1;   // P (present)
    uint16_t offset_high;     // alamat handler bit 31:16
};
```
Jika dirangkai, byte tipe = `1000 1110` (`0x8E`) untuk interrupt gate DPL 0. Untuk syscall `int 0x30`, `dpl=3` agar user boleh memanggil.

**Kenapa alamat dipecah dua?** Warisan arsitektur 286 → 386 yang mempertahankan kompatibilitas: field 16-bit di posisi lama, sisanya ditaruh belakangan.

```c
void initialize_idt(void) {
    for (uint32_t i = 0; i < ISR_STUB_TABLE_LIMIT; i++)
        set_interrupt_gate(i, isr_stub_table[i], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 0);
    __asm__ volatile("lidt %0" : : "m"(_idt_idtr));  // beri tahu CPU lokasi IDT
    __asm__ volatile("sti");                          // nyalakan interrupt
}
```
- Loop 0..63 mengisi tiap entri dengan alamat stub dari `isr_stub_table` (dibuat di assembly).
- `lidt` = load IDT register. Format operandnya `size + address` (sama seperti GDTR).
- `sti` = set interrupt flag; tanpa ini IRQ tidak akan pernah masuk.

```c
void set_interrupt_gate(uint8_t int_vector, void *handler_address,
                        uint16_t gdt_seg_selector, uint8_t privilege) {
    struct IDTGate *g = &interrupt_descriptor_table.table[int_vector];
    uint32_t handler = (uint32_t)(uintptr_t) handler_address;

    g->offset_low  = handler & 0xFFFF;
    g->offset_high = (handler >> 16) & 0xFFFF;
    g->segment     = gdt_seg_selector;
    g->dpl         = privilege;

    g->_reserved = 0;
    g->_r_bit_1  = INTERRUPT_GATE_R_BIT_1;   // 0b000
    g->_r_bit_2  = INTERRUPT_GATE_R_BIT_2;   // 0b110
    g->_r_bit_3  = INTERRUPT_GATE_R_BIT_3;   // 0b0
    g->gate_32   = 1;
    g->valid_bit = 1;
}
```
- Alamat 32-bit dipecah dengan **masking** (`& 0xFFFF`) dan **shifting** (`>> 16`).
- Setiap field ditulis agar sesuai Gambar 6-2 Intel.

---

## 3.5 `intsetup.s` — jembatan hardware → C

Hardware mendorong `eflags, cs, eip` (+`ss, esp` bila ganti ring) dan (untuk exception tertentu) `error_code`. Tugas assembly:
1. Menyeragamkan stack (yang tidak punya error code, didorong `0`).
2. Menyimpan nomor interrupt.
3. Menyimpan seluruh register.
4. Memanggil `main_interrupt_handler` (C).
5. Memulihkan semuanya dan `iret`.

```asm
%macro no_error_code_interrupt_handler 1
interrupt_handler_%1:
    push    dword 0        ; error code palsu
    push    dword %1       ; nomor interrupt
    jmp     call_generic_handler
%endmacro
```
Makro membuat 64 stub: `interrupt_handler_0` … `interrupt_handler_63`. Dengan begitu semua interrupt punya kerangka yang sama.

```asm
call_generic_handler:
    push ds
    push es
    push fs
    push gs
    pushad                     ; eax,ecx,edx,ebx,esp,ebp,esi,edi
    ...
    call main_interrupt_handler
    ...
    add esp, 8                 ; buang int_number + error_code
    iret
```
- `pushad` = simpan 8 register umum (urutan CPU: eax dulu → alamat tertinggi; hasil di memory: edi terendah … eax tertinggi).
- Setelah handler C selesai: `popad`, `pop` segmen, buang 8 byte, lalu `iret` memulihkan `eip/cs/eflags` (dan `esp/ss` bila kembali ke ring 3).

Versi **correction** (dipakai mulai Bab 5) menambahkan:
- Menyalin `esp` milik user dari interrupt stack ke slot `CPURegister.stack.esp` saat inter-privilege (agar kernel tahu stack user untuk `iret` nanti).
- `mov ebp, esp` + parameter pointer ke frame agar handler C bisa **mengubah** nilai register (mis. `eax` hasil syscall).

`isr_stub_table` di akhir file = array 64 alamat stub; inilah yang dibaca `initialize_idt()`.

---

## 3.6 `interrupt.c` — PIC remap dan handler utama

```c
void io_wait(void) { out(0x80, 0); }
```
Tulis ke port `0x80` (tak terpakai) = tunda 1–4 mikrodetik; PIC butuh jeda antar perintah.

```c
void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);  io_wait();
    out(PIC1_DATA, PIC1_OFFSET);   // IRQ master mulai 0x20
    out(PIC2_DATA, PIC2_OFFSET);   // IRQ slave mulai 0x28
    out(PIC1_DATA, 0b0100);        // slave terpasang di IRQ2 master
    out(PIC2_DATA, 0b0010);        // identitas cascade slave = 2
    out(PIC1_DATA, ICW4_8086);     // mode 8086
    out(PIC2_DATA, ICW4_8086);
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK); // semua IRQ dimatikan dulu
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}
```
- ICW1: mulai inisialisasi. ICW2: **offset vektor** (inilah "remap"). ICW3: hubungan master–slave. ICW4: mode operasi.
- Semua IRQ dimatikan dulu; nanti dinyalakan satu per satu (keyboard di fungsi berikut).

```c
void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);  // slave perlu ACK sendiri
    out(PIC1_COMMAND, PIC_ACK);
}
void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}
```
- ACK/EOI: `0x20` ke port command.
- Nyalakan IRQ keyboard: baca mask, matikan bit 1 (`~(1<<1)`), tulis kembali. Bit 0 pada mask = IRQ diperbolehkan.

```c
void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER:      // 0x20
            pic_ack(IRQ_TIMER);
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:   // 0x21
            keyboard_isr();
            break;
        default:
            break;
    }
}
```
Versi M1 seperti di atas (timer hanya ACK; keyboard panggil ISR). Versi final menambahkan:
- `keyboard_isr` sendiri yang ACK (karena aturan "semua IRQ1 wajib ACK").
- IRQ0 memanggil scheduler (Bab 6).
- `int 0x30` (48) memanggil `syscall` (Bab 5).
- Handler page fault / GPF untuk mematikan proses yang salah (Bab 6).

---

## 3.7 `keyboard.h/c` — dari scancode ke huruf

### Konsep
- Keyboard mengirim **IRQ1** saat ada tombol ditekan/dilepas.
- Data yang dibaca dari port `0x60` = **scancode** (kode posisi tombol), bukan karakter.
- **Make** = ditekan, **break** = dilepas (bit ke-7 = 1, mis. `'A'` make `0x1E`, break `0x9E`).
- Setiap IRQ1 **wajib** membaca port `0x60` (kalau tidak, buffer internal keyboard penuh dan keyboard "macet").

### Tabel scancode
```c
const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', ... 'q', 'w', ... 
};
```
Array 256 entri: `map[scancode]` → ASCII. Entri `0` berarti tidak menghasilkan karakter (mis. tombol modifier, break code, tombol tak dikenal). Ini hanya layout **make**; break code punya indeks ≥ 0x80 dan isinya 0.

Versi final menambahkan `keyboard_scancode_1_to_ascii_shift_map` (huruf besar dan simbol `!@#$%^&*()_+{}|:"<>?~`) supaya shell bisa mengetik `|`, `&`, `/` dengan benar.

### State
```c
static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = 0
};
static bool keyboard_shift_pressed = false;
```
- `read_extended_mode`: penanda bahwa byte `0xE0` sudah diterima (awalan scancode extended seperti tombol panah).
- `keyboard_input_on`: apakah ISR boleh menyimpan hasil ke buffer.
- `keyboard_buffer`: satu karakter terakhir; dibaca oleh `get_keyboard_buffer` lalu dikosongkan.

```c
void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;   // salin keluar
    keyboard_state.keyboard_buffer = 0;      // kosongkan (flush)
}
```
Ini pola "polling buffer": kernel/shell memanggil berulang, kalau `*buf != 0` berarti ada input.

### ISR
```c
void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);        // WAJIB baca tiap IRQ1

    if (scancode == EXTENDED_SCANCODE_BYTE) {          // 0xE0
        keyboard_state.read_extended_mode = true;
    } else {
        bool is_extended = keyboard_state.read_extended_mode;
        keyboard_state.read_extended_mode = false;

        if (!is_extended) {
            if (scancode == LSHIFT || scancode == RSHIFT)
                keyboard_shift_pressed = true;
            else if (scancode == LSHIFT_RELEASE || scancode == RSHIFT_RELEASE)
                keyboard_shift_pressed = false;
            else if (keyboard_state.keyboard_input_on) {
                char c = keyboard_shift_pressed
                       ? shift_map[scancode] : map[scancode];
                if (c) keyboard_state.keyboard_buffer = c;
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);                             // ACK di akhir (selalu)
}
```
Baris demi baris:
- Baca scancode — memenuhi aturan "harus baca port".
- `0xE0`: tandai extended, **jangan** map sekarang; byte berikutnya yang diproses/diabaikan.
- Shift make/release: update `keyboard_shift_pressed` (modifier tidak menghasilkan karakter).
- Jika input off: tetap ACK, tidak menyimpan.
- Karakter disimpan ke buffer **satu per satu**; tidak ada loop di ISR (non-blocking, sesuai spec).
- `if (c)` mencegah menimpa buffer dengan 0.
- ACK dilakukan **di dalam ISR** agar selalu terjadi, baik input on/off.

**Demo**:
1. "Kenapa tiap IRQ1 harus `in` port 0x60?" → Karena keyboard controller menahan IRQ berikutnya sampai buffer-nya dibaca.
2. "Kenapa break code diabaikan?" → Kita hanya bereaksi pada penekanan; bit-7 menandakan pelepasan. Untuk `get_keyboard_buffer` perilaku ini cukup; modifier ditangani khusus.
3. "Kenapa tidak boleh loop di ISR?" → ISR menahan CPU; loop membuat interrupt lain tertunda dan bisa kehilangan data keyboard.

---

## 3.8 Alur lengkap satu ketikan (hafalkan untuk demo)

```
tombol ditekan
  → keyboard controller kirim IRQ1
  → PIC (sudah di-remap) meneruskan vektor 0x21 ke CPU
  → CPU simpan eflags/cs/eip ke stack, baca IDT[0x21]
  → lompat ke isr_stub_table[0x21] = interrupt_handler_33
  → stub: push 0, push 33, jmp call_generic_handler
  → call_generic_handler: push segmen + pushad, set ds/es/fs/gs=0x10
  → call main_interrupt_handler(&frame)
      → case 0x21: keyboard_isr()
          → in(0x60) → map scancode → keyboard_buffer = 'a'
          → pic_ack(1)
  → popad + pop segmen + add esp,8 + iret
  → CPU lanjutkan instruksi yang tertunda
shell (di user mode) memanggil getchar (syscall) → membaca buffer
```

---

## 3.9 Verifikasi Ch1

```bash
make prepare && make run     # lalu ketik di QEMU
```
Yang dibuktikan: "Hai!" tampil (M1), dan setelah Bab 5 shell menerima ketikan. Untuk latihan debug:
- breakpoint di `main_interrupt_handler`, cek `frame.int_number == 0x21` saat menekan tombol;
- `info registers` → lihat `cs=0x8` (kernel) saat interrupt;
- breakpoint `keyboard_isr` → lihat `scancode` berubah, lalu `keyboard_buffer`.

---

## 3.10 Tanya-Jawab Demo Ch1

1. **Kenapa framebuffer 2 byte per karakter?** 1 byte ASCII + 1 byte atribut warna (format hardware VGA text mode).
2. **Kenapa kursor pakai port, bukan MMIO?** Kontrol kursor VGA memang lewat register I/O `0x3D4/0x3D5` (legacy hardware).
3. **Apa itu PIC remap dan kenapa 0x20?** Menggeser IRQ agar tidak bertabrakan dengan exception CPU 0x00–0x1F; IRQ0 jadi 0x20.
4. **Apa bedanya interrupt gate dan trap gate?** Interrupt gate mematikan IF saat masuk handler (tidak bisa disela IRQ lain); trap gate tidak.
5. **Kenapa `pic_ack` penting?** Tanpa EOI, PIC menganggap IRQ belum selesai dan memblokir IRQ dengan prioritas ≤ yang sedang diproses.
6. **Kenapa `pushad` dan `popad`?** Menyimpan/memulihkan seluruh register yang mungkin dirusak handler C.
7. **Kenapa 64 stub, bukan 256?** Cukup untuk exception 0–31 dan IRQ 32–63; IDT tetap 256 entri tapi sisanya tidak dipakai.
8. **Bagaimana extended scancode (panah) ditangani?** Byte `0xE0` menandai mode extended; byte berikutnya diabaikan (tidak dipetakan ke ASCII) — spec hanya butuh ASCII printable.

---

Bab berikutnya: `04-ch2-disk-ext2.md` — ATA PIO baris per baris, seluruh struktur EXT2, dan cara `read`/`write`/`delete` bekerja sampai level blok.
