# Bab 5 — Chapter 3: Paging, User Mode, Syscall, dan Shell

Tujuan Chapter 3: kernel pindah ke **higher half**, tiap program punya **ruang alamat sendiri**, CPU bisa berpindah **ring 0 ↔ ring 3**, dan kita punya **shell** yang jalan sebagai program user.

File: `linker.ld` (higher-half), `kernel-entrypoint.s` (bootstrap), `paging.h/c`, `gdt.h/c` (TSS + user segment), `idt.h/c` (gate syscall), `intsetup.s` (koreksi), `interrupt.c` (dispatcher syscall), `crt0.s`, `user-linker.ld`, `shell.c`, plus aturan Makefile untuk program user.

---

## 5.1 Kenapa higher half? (VMA vs LMA)

- Tanpa paging, kernel di `0x00100000` (1 MiB) dan program user juga harus di alamat rendah → rawan bertabrakan.
- Solusi: kernel dipetakan di paruh atas (`0xC0000000+`), user di paruh bawah (`0x00000000–0xBFFFFFFF`).
- `linker.ld` final: kode "hidup" di VMA `0xC0100000`, tetapi **dimuat** GRUB di LMA `0x00100000` (`AT (ADDR(...) - 0xC0000000)`).
- Selama bootstrap, paging pertama memetakan **identitas** (`virt == phys`) sekaligus **higher half** (`virt = phys + 0xC0000000`), lalu identitas dihapus setelah lompat.

---

## 5.2 Bootstrap paging di `kernel-entrypoint.s` (final)

```asm
loader equ (loader_entrypoint - KERNEL_VIRTUAL_BASE)
loader_entrypoint:
    mov edi, (_paging_kernel_page_table - KERNEL_VIRTUAL_BASE)
    xor eax, eax
    mov ecx, 1024
.fill_kernel_page_table:
    mov edx, eax
    or  edx, 0x3           ; present + writable
    mov [edi], edx         ; PTE[i] = frame i, flag 0x3
    add edi, 4
    add eax, 0x1000
    loop .fill_kernel_page_table
```
- Isi **1024 entri page table** secara identitas: entri ke-i menunjuk frame fisik `i*4 KiB`. Hasilnya 4 MiB pertama terpetakan.
- Alamat-alamat dikurangi `0xC0000000` karena saat ini masih berjalan di alamat fisik (belum ada paging).

```asm
    mov eax, (_paging_kernel_page_table - KERNEL_VIRTUAL_BASE)
    or  eax, 0x3
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE)], eax       ; PDE[0]  → identitas
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xC00)], eax ; PDE[0x300] → 0xC0000000
    mov eax, (_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE)
    or  eax, 0x3
    mov [(_paging_kernel_page_directory - KERNEL_VIRTUAL_BASE + 0xFFC)], eax ; PDE[1023] → dirinya sendiri
```
- `PDE[0x300]`: indeks `0xC0000000 >> 22 = 0x300`, membuat 4 MiB pertama muncul lagi di `0xC0000000`.
- `PDE[1023]` (`0xFFC` byte): **recursive mapping** — page directory menunjuk dirinya sendiri. Ini kunci agar kode C bisa mengakses page table lewat alamat virtual `0xFFC00000 + indeks*0x1000` (lihat 5.4).

```asm
    mov cr3, eax           ; aktifkan page directory
    mov eax, cr4
    and eax, 0xFFFFFFEF    ; matikan PSE (jangan pakai 4 MiB page)
    mov cr4, eax
    mov eax, cr0
    or  eax, 0x80000000    ; bit PG = paging ON
    mov cr0, eax
    lea eax, [loader_virtual]
    jmp eax                ; lompat ke alamat virtual (higher half)
loader_virtual:
    mov dword [(_paging_kernel_page_directory)], 0   ; hapus mapping identitas PDE[0]
    invlpg [0]                                        ; invalidasi TLB
    mov esp, kernel_stack + KERNEL_STACK_SIZE
    call kernel_setup
```
- Setelah `cr0.PG=1`, alamat yang dieksekusi masih identitas; far jump ke `loader_virtual` memindahkan eksekusi ke alamat virtual.
- `invlpg [0]`: buang entri TLB untuk alamat 0 agar pemetaan identitas benar-benar hilang.
- Stack baru disiapkan di higher half; baru panggil C.

---

## 5.3 `paging.h` — struktur (baris penting)

Flag (8 bit pertama, sama untuk PDE/PTE):
```c
struct PageDirectoryEntryFlag {
    uint32_t present_bit : 1;   // P
    uint32_t write_bit   : 1;   // R/W
    uint32_t us_bit      : 1;   // U/S (1 = user)
    uint32_t pwt_bit     : 1;
    uint32_t pcd_bit     : 1;
    uint32_t accessed_bit: 1;
    uint32_t dirty_bit   : 1;
    uint32_t use_pagesize_4_mb : 1; // PS (harus 0 = 4 KiB)
};
```
Entry 32-bit:
```c
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;     // bit 0..7
    uint16_t global_page    : 1;            // bit 8 G
    uint16_t ignored        : 3;            // bit 9..11
    uint16_t page_attr_table: 1;            // bit 12
    uint16_t higher_address : 8;            // bit 13..20 (alamat bit 13..20)
    uint16_t reserved       : 1;            // bit 21 (alamat bit 21)
    uint16_t page_table_base_address : 10;  // bit 22..31 (alamat bit 22..31)
};
```
- Nomor frame 20-bit **terbelah** di beberapa field. Karena itu `paging.c` menulis/membaca entry sebagai **satu word 32-bit** (lihat 5.4), bukan mengisi bitfield satu-satu.
- `PageTable` = array 1024 `PageTableEntry`; `PageDirectory` = array 1024 `PageDirectoryEntry`.
- `PageManagerState`: `bool page_frame_map[32768]` (true = terpakai) + `free_page_frame_count`.

Alamat penting:
- `KERNEL_VIRTUAL_ADDRESS_BASE = 0xC0000000`
- `KERNEL_RESERVED_PAGE_FRAME_COUNT = 0x400000/0x1000 = 1024` (4 MiB pertama milik kernel/GRUB/MMIO)
- `RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS = 0xFFC00000`
- `PAGE_FRAME_MAX_COUNT = 128 MiB / 4 KiB = 32768`

---

## 5.4 `paging.c` — bedah fungsi

### Helper tulis/baca word (penting!)
```c
static void paging_write_entry(void *entry, uint32_t frame_number, uint8_t flags) {
    uint32_t word = ((frame_number & 0xFFFFF) << 12) | (flags & 0xFF);
    memcpy(entry, &word, sizeof(word));
}
static uint32_t paging_read_entry_frame(const void *entry) {
    uint32_t word; memcpy(&word, entry, sizeof(word));
    return word >> 12;
}
```
- Menyusun entry sebagai satu word: frame (20 bit) di atas, flag (8 bit) di bawah.
- **Kenapa `memcpy`?** Menghindari aliasing & masalah bitfield; compiler tidak boleh berasumsi apa-apa tentang layout bitfield.

### `paging_get_page_table(page_dir, index)`
```c
if (index >= 1024 || index == RECURSIVE_PAGE_DIRECTORY_INDEX
    || page_dir != paging_get_current_page_directory_addr()) return NULL;
struct PageDirectoryEntry *dir_entry = &page_dir->table[index];
if (dir_entry->flag.present_bit)
    return (struct PageTable *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + index * PAGE_FRAME_SIZE);
```
- Hanya bekerja untuk page directory yang **sedang aktif** (recursive mapping hanya menggambarkan dirinya sendiri).
- Jika PDE sudah present → alamat table lewat rumus recursive: `0xFFC00000 + index*0x1000`.
- Jika belum: cari frame bebas pertama (>= reserved), tandai terpakai, tulis PDE (present/write/user), `invlpg`, `memset` table ke 0, kembalikan.

**Cara kerja alamat `0xFFC00000`** (hafalkan untuk demo):
- `0xFFC00000 >> 22 = 1023` → CPU memakai PDE[1023] untuk halaman ini.
- PDE[1023] menunjuk page directory **itu sendiri**.
- `(0xFFC00000 >> 12) & 0x3FF = 0` → offset dalam page table = 0, jadi CPU membaca page directory sebagai "page table".
- Untuk mendapat **page table** indeks `i`: lihat sebagai alamat `0xFFC00000 + i*0x1000`; PDE[1023] → dir, PTE=dir[i] = page table ke-i. Itulah trik **fractal page mapping**.

### `update_page_directory_entry(_internal)`
```c
pde_index = (virt >> 22) & 0x3FF;
pte_index = (virt >> 12) & 0x3FF;
page_table = paging_get_page_table(page_dir, pde_index);
paging_write_entry(&page_table->table[pte_index], phys >> 12, flags);
flush_single_tlb(virt);
```
- Ahli bedah utama: memecah alamat virtual 10/10/12 bit, memastikan page table ada, tulis PTE, lalu flush TLB.

### `paging_allocate_check(amount)`
```c
required = ceil(amount / 4096);
return free_page_frame_count >= required;
```
- Cek cepat apakah cukup frame.

### `paging_allocate_user_page_frame`
1. First-fit cari frame bebas dari 1024..32767.
2. Tandai terpakai, kurangi counter.
3. `update_page_directory_entry_internal(page_dir, frame*4096, virt, user_flag)`; user flag = P=1, R/W=1, U/S=1.
4. Jika gagal mapping → kembalikan frame (rollback).
5. `memset(virt, 0, 4096)` — frame tidak dijamin nol setelah dialokasikan.

### `paging_free_user_page_frame`
- Hitung indeks, pastikan PDE/PTE ada dan bukan 4 MiB; ambil nomor frame; tandai bebas + counter++; nolkan PTE (`memcpy(entry,&zero,4)`), `invlpg`.

### `paging_create_new_page_directory`
- Cari slot `page_directory_list` kosong; nolkan; **salin PDE[0x300] kernel** agar higher-half ikut terpetakan di proses baru; isi PDE[1023] dengan alamat fisik dir ini sendiri (`&dir - 0xC0000000`, flag 0x3) sehingga recursive mapping bekerja per proses; kembalikan alamat virtual dir.
- `paging_free_page_directory`: bebaskan semua page table (kecuali 0x300 & 1023), nolkan dir, tandai slot bebas.

### `paging_get_current_page_directory_addr` / `paging_use_page_directory`
```c
__asm__ volatile("mov %%cr3, %0" : "=r"(addr));       // baca CR3 (fisik)
return (struct PageDirectory*)(addr + 0xC0000000);     // konversi ke virtual
...
phys = (uint32_t)dir; if (dir >= 0xC0000000) phys -= 0xC0000000;
__asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory"); // ganti address space
```
- CR3 menyimpan alamat **fisik**; kode mengaksesnya lewat alamat **virtual** (higher half). `paging_use_page_directory` = "ganti address space" (dipakai scheduler).

---

## 5.5 TSS dan GDT user (`gdt.h/c`, `interrupt.h`)

- Saat interrupt dari ring 3, CPU butuh **stack kernel**. Alamatnya disimpan di **TSS (`esp0`)**, dan CPU tahu TSS lewat register task (`ltr`).
- `struct TSSEntry` (di `interrupt.h`) berisi antara lain `ss0` (selector data kernel) dan `esp0` (stack kernel).
- `gdt_install_tss()` menambal descriptor TSS (base dipecah 3 field — Bab 2).
- `set_tss_register()` (assembly) = `ltr 0x28`.
- `set_tss_kernel_current_stack()`:
```c
__asm__ volatile ("mov %%ebp, %0" : "=r"(stack_ptr));   // ambil base frame kernel_setup
_interrupt_tss_entry.esp0 = stack_ptr + 8;               // +8: return address + local var
```
Tujuannya: setiap syscall dari user memakai stack kernel yang sama, sehingga data kernel tetap aman.
- GDT user code `0x18` (DPL 3) dan user data `0x20` (DPL 3) memungkinkan `iret` menurunkan privilege.

---

## 5.6 IDT syscall (`idt.c`)

Selain 64 vektor stub, kernel final menambahkan gate khusus:
```c
set_interrupt_gate(0x30, isr_stub_table[0x30], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 3);
```
- `0x30` (48) = nomor syscall; **DPL 3** agar boleh dipanggil dari user (`int 0x30`).
- Vektor lain DPL 0 (hanya kernel).
- Handler-nya tetap stub biasa; `intsetup.s` mengarahkan ke `main_interrupt_handler`, yang melihat `int_number == 0x30`.

### Koreksi `intsetup.s` (wajib untuk user mode)
- Saat interrupt inter-privilege, CPU mendorong `ss` dan `esp` user. Stub menyalin `esp` user itu ke slot `CPURegister.stack.esp`.
- Menyimpan `ebp` sebagai base frame dan mengirim **pointer** frame ke handler C, sehingga dispatcher bisa **menulis balik `eax`** (nilai kembalian syscall) ke konteks user.
- Tanpa koreksi ini, `int 0x30` dari shell akan kehilangan stack/kembalian.

---

## 5.7 Syscall (`interrupt.c`) — kontrak `int 0x30`

Konvensi: `eax` = layanan, `ebx/ecx/edx` = argumen. Nilai kembali di `eax`.

| eax | Layanan | ebx | ecx | edx |
|---|---|---|---|---|
| 0 | FS `read` | `EXT2DriverRequest*` | `int8_t* ret` | — |
| 1 | FS `read_directory` | request | `int8_t* ret` | — |
| 2 | FS `write` | request | `int8_t* ret` | — |
| 3 | FS `delete` | request | `int8_t* ret` | — |
| 4 | keyboard `getchar` | `char* out` | — | — |
| 5 | `putchar` | char | warna | — |
| 6 | `puts` | `char*` | jumlah | warna |
| 7 | aktifkan keyboard | — | — | — |
| 8 | FS `get_file_size` | request | `uint32_t* out` | `int8_t* ret` |
| 9 | clear screen | — | — | — |
| 10 | `exit` | kode | — | — |
| 11 | `exec` | `char* path` | `char** argv` | flags |
| 12 | `kill` | pid | — | — |
| 13 | `ps` | `char* buf` | kapasitas | — |
| 14 | RTC time | `RTCTime*` | — | — |
| 15/16 | set/get cursor | row/col | — | — |
| 17 | `getpid` | — | — | — |
| 18 | tulis di posisi | `(row<<8)|col` | `char*` | `(warna<<8)|panjang` |

Bagian awal dispatcher:
```c
struct EXT2DriverRequest *request = (void*) frame->cpu.general.ebx;
uint32_t service = frame->cpu.general.eax;
frame->cpu.general.eax = 0;
switch (service) { ... }
```
- `eax` dikosongkan dulu sebagai nilai default (0) supaya layanan tanpa return tetap aman.

### Keamanan: validasi pointer user
```c
static bool is_user_pointer(const void *ptr) {
    return ptr != NULL && (uint32_t) ptr < KERNEL_VIRTUAL_ADDRESS_BASE;
}
static bool is_valid_fs_request(request, name_optional, need_buffer) { ... }
```
- **Wajib**: semua pointer dari user divalidasi agar user tidak menyuruh kernel membaca/menulis alamat kernel (privilege escalation). Pointer user harus `< 0xC0000000`.
- `is_valid_fs_request` memeriksa request, parent_inode, name/name_len, dan buf sesuai kebutuhan layanan.

### Layanan penting
- **read/readdir/write/delete/file_size**: memanggil fungsi EXT2 yang sama (Bab 4) lalu menulis kode error ke pointer `int8_t*` user.
- **getchar**: `get_keyboard_buffer((char*)ebx)` — mengambil 1 karakter; shell memanggil berulang (polling).
- **putchar/puts**: output lewat framebuffer (kernel), termasuk handle `\n`, `\b`, scroll.
- **exit**: `process_destroy(pid)`, lalu `scheduler_switch_to_next_process()` — proses tidak kembali.
- **exec**: `resolve_exec_path` mengurai path absolut (`/bin/clock`) menjadi `parent_inode` + nama; kumpulkan `argv` user (sampai 8); panggil `process_create_user_process_args`; kembalikan PID baru di `eax`. Catatan: eksekusi bersifat **background** (pemanggil lanjut), tidak menunggu anak.
- **kill**: `process_destroy(pid)`; jika yang dibunuh adalah proses pemanggil, langsung switch ke proses lain.
- **ps**: iterasi `_process_list`, format `"PID <pid>  <nama>  <STATE>\n"` ke buffer user.
- **get_time**: `cmos_read_rtc` (Bab 6).
- **write_at**: khusus `clock` — menulis di posisi layar tanpa menggeser kursor (dipakai untuk menampilkan jam di pojok).
- **resolve_exec_path**: mulai dari inode 1 (root), iterasi segmen path dengan `get_entry_info`; segmen terakhir jadi nama file; setiap segmen antara harus direktori.

### Exception dari user
- `main_interrupt_handler` memeriksa `cs & 0x3`: kalau 3 (user) → `user_fault_exit()` mematikan proses yang bersalah (page fault/GPF) dan lanjut ke proses lain, bukan mematikan OS.
- Page fault: baca `cr2` (alamat penyebab) lalu cetak.

---

## 5.8 Toolchain program user

`crt0.s` (entry setiap program user):
```asm
global _start
extern main
_start:
    mov ebp, esp      ; base frame
    call main         ; main(argc, argv) — argumen sudah disiapkan kernel di stack!
    mov ebx, eax      ; kode keluar = return main
    mov eax, 10       ; SYS_EXIT
    int 0x30
    cli; hlt          ; jangan pernah sampai sini
```
- Kernel (`build_user_stack` di `process.c`) menyusun stack mengikuti cdecl: `[esp]=argc`, `[esp+4]=argv`, lalu string argumen. Jadi `main(int,char**)` langsung benar.

`user-linker.ld`:
- `. = 0x00000000`: program user di-link ke alamat 0 (kernel memuatnya di sana).
- `*crt0.o(.text)` diletakkan **paling depan** agar entry di offset 0.
- `ASSERT(_linker_user_program_end <= 1 MiB)` — batas ukuran program.
- `.bss` diletakkan terakhir; kernel tahu ukurannya dari `i_size` (karena Makefile mengubah `.bss` jadi bagian binary dengan `objcopy --set-section-flags .bss=alloc,load,contents`), sehingga BSS ikut termuat dan otomatis nol.

Makefile (ringkas):
```make
$(OUTPUT_FOLDER)/%_elf: $(USER_FOLDER)/%.c $(USER_FOLDER)/crt0.s
	$(ASM) crt0.s -o crt0.o
	$(CC) -m32 -fno-pie $< -o program.o
	$(CC) ... string.c -o string.o
	$(LIN) -T user-linker.ld crt0.o program.o string.o -o $@
$(OUTPUT_FOLDER)/%: $(OUTPUT_FOLDER)/%_elf
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents $< $@
```
- `-fno-pie`: tanpa position-independent code (alamat absolut).
- `objcopy -O binary`: ubah ELF → binary pipih yang bisa dibaca kernel via EXT2.

---

## 5.9 `shell.c` — bedah struktur (1180 baris)

Urutan bagian file (setiap bagian dijelaskan fungsinya):

1. **Syscall wrapper** (`syscall`, `syscall_read/read_dir/write/delete`, `query_file_size`):
   `static uint32_t syscall(eax,ebx,ecx,edx) { asm volatile("int $0x30" : : "a"(eax),"b"(ebx),"c"(ecx),"d"(edx) : "memory"); }`
   — satu pintu ke kernel; sisanya pembungkus agar kode mudah dibaca.
2. **String utility** (`str_len/str_eq/str_copy/str_append/str_chr`): implementasi sendiri karena tidak ada libc.
3. **Buffer global**: `io_buffer` (pipeline), `dir_buffer` (isi direktori), `file_buffer` (isi file), `line_buffer` (baris input).
4. **State cwd**: `cwd_path` (`"/"`), `cwd_inode` (1 = root), `path_dirs` (untuk `find`).
5. **Output** (`out_char/out_str/out_newline/print_error`): memakai syscall `putchar`/`puts` dengan warna.
6. **Dirent helper** (`entry_at`, `entry_name`, `is_dot_entry`): membaca entri dari `dir_buffer`.
7. **Operasi FS tingkat tinggi**: `load_directory` (syscall readdir → `dir_buffer`), `find_in_dir` (cari nama → inode+tipe), `write_file`, `make_directory`, `delete_entry`, `read_file`, `query_file_size`.
8. **Path handling**: `path_push/path_pop` (cwd), `dir_child_inode`, `resolve_parent(path)`, `file_exists`, `make_absolute`.
9. **PATH / perintah eksternal** (bonus): `resolve_executable` (cari di PATH), `path_set_default/path_add_entry/path_load/path_save/print_path/path_set`; PATH disimpan di `/path.cfg` dan dimuat saat shell mulai.
10. **Rekursif** (bonus): `collect_children`, `copy_tree`, `remove_tree` untuk `cp`/`rm` folder.
11. **Perintah**: `cmd_help`, `cmd_ls`, `cmd_cd`, `cmd_mkdir`, `cmd_cat`, `cmd_cp`, `cmd_rm`, `cmd_mv`, `cmd_find`, `cmd_grep`, `cmd_exec`, `cmd_ps`, `cmd_kill`.
12. **Routing**: `is_builtin`, `run_command` (pipeline `a | b` lewat `pipe_out/pipe_in`, lalu eksekusi builtin atau eksternal), `run_line` (pisah `&` → background), `print_prompt`.
13. **`read_line`**: loop `getchar`; menangani Enter, backspace, echo karakter; mengembalikan panjang baris.
14. **`main`**: aktifkan keyboard (syscall 7), clear layar, banner; loop: prompt → baca baris → `run_line`.

### Alur satu perintah, mis. `cat readme.txt`
```
read_line() kumpulkan "cat readme.txt\n"
run_line() → run_command() → bukan pipeline
is_builtin("cat") → true
cmd_cat("readme.txt")
  → query_file_size(name, cwd_inode)      [syscall 8]
  → read_file(name, cwd_inode, file_buffer, FILE_BUFFER_SIZE)  [syscall 0 → EXT2 read]
  → cetak isi lewat syscall puts
```

### Perintah eksternal via PATH (mis. `hello`)
```
run_command("hello") → bukan builtin
resolve_executable("hello") → "/bin/hello" (dari PATH)
→ syscall exec(path, argv) [eax=11]
   → kernel resolve path, process_create_user_process_args()
   → proses baru READY; scheduler akan menjalankannya
→ shell langsung menerima prompt lagi (background)
```

---

## 5.10 Verifikasi Ch3

- `make prepare` → `bin/shell`; `ls`, `cat`, `mkdir`, `cd`, `cp`, `rm`, `mv`, `find`, `grep`, pipeline teruji di QEMU.
- Debug: `int 0x30` breakpoint di `syscall()`, cek `frame->cpu.general.eax` (nomor layanan) dan `ebx` (pointer). Breakpoint di `paging_get_page_table` untuk melihat recursive mapping bekerja saat halaman pertama dialokasikan.

## 5.11 Tanya-Jawab Demo Ch3

1. **Kenapa kernel dipindah ke higher half?** Memisahkan kernel (atas) dan user (bawah) agar tiap proses punya ruang alamat sendiri.
2. **Apa itu recursive page mapping dan kenapa perlu?** Agar kernel bisa mengubah page table/page directory lewat alamat virtual tanpa harus tahu alamat fisiknya; perlu karena PDE[1023] menunjuk dir sendiri dan `0xFFC00000` dipakai mengaksesnya.
3. **Apa fungsi `invlpg`?** Menghapus 1 entri TLB setelah mengubah mapping; tanpa itu CPU bisa memakai mapping lama.
4. **Kenapa syscall butuh gate DPL 3?** Karena `int 0x30` dieksekusi dari ring 3; gate DPL 0 akan menghasilkan #GP.
5. **Kenapa TSS diperlukan?** Menyediakan `esp0` (stack kernel) saat CPU berpindah ring 3→0.
6. **Kenapa pointer user divalidasi?** Mencegah user membaca/menulis alamat kernel (privilege escalation).
7. **Apa isi stack program user saat `main` dipanggil?** `argc`, pointer `argv`, lalu string argumen — disusun `build_user_stack()`.
8. **Kenapa `objcopy --set-section-flags .bss=...`?** Agar BSS ikut masuk binary pipih (isi nol) sehingga kernel tidak perlu tahu ukurannya terpisah.

---

Bab berikutnya: `06-ch4-process-scheduler.md` — proses, PCB, context switch, PIT, dan shell multitasking.
