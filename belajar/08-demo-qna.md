# Bab 8 — Kumpulan Tanya-Jawab Demo/Ujian

Bab ini merangkum pertanyaan yang paling mungkin ditanya saat asistensi/demo, dikelompokkan per topik, termasuk pertanyaan "jebakan" dan cara menjawabnya. Ditutup dengan **daftar hafalan minimal** dan **latihan alur**.

---

## 8.1 Pertanyaan Lintas-Bab (paling sering)

1. **Jelaskan alur boot dari power-on sampai shell!**
   BIOS/UEFI → POST → cari boot device (MBR/GPT) → GRUB (grub1 El Torito) → baca `menu.lst` → muat `/boot/kernel` (ELF, multiboot header valid) → `loader`:
   bootstrap paging → higher-half jump → `load_gdt` → `kernel_setup`:
   PIC remap, IDT, keyboard, framebuffer, EXT2, TSS, buat proses shell, `scheduler_init`, `switch_to_next` → `iret` ke ring 3 → shell → syscall.

2. **Di file mana titik masuk kernel?** `src/kernel-entrypoint.s` label `loader` (ditunjuk `ENTRY(loader)` di `linker.ld`); di situ `kernel_setup()` (C) dipanggil.

3. **Apa saja mode CPU yang dilewati?** Real mode (BIOS/GRUB) → protected mode 32-bit (`load_gdt`) → setelah `iret` ke `cs=0x1B`: user mode ring 3.

4. **Kapan paging dinyalakan?** Sebelum masuk kernel C, di `kernel-entrypoint.s` (`cr0.PG`), setelah menyiapkan page directory/table dan recursive PDE[1023].

5. **Apa yang terjadi saat mengetik satu huruf di shell?**
   Keyboard IRQ1 → PIC 0x21 → IDT stub → `keyboard_isr` (baca port 0x60, map scancode, simpan buffer, ACK) → shell polling `getchar` (syscall 4) → echo → Enter → `run_line` → syscall FS/output.

6. **Apa yang terjadi saat `cat readme.txt`?**
   Shell → syscall 8 (`get_file_size`) → syscall 0 (`read`) → kernel `read()` EXT2 (scan dirent, baca inode, salin blok) → shell cetak via syscall 6 (`puts`).

7. **Apa yang terjadi saat IRQ0 timer?**
   PIT → PIC → IDT 0x20 → `main_interrupt_handler`: ACK → snapshot `Context` dari frame → simpan ke PCB sekarang (state READY) → `scheduler_switch_to_next_process` (round-robin, ganti CR3) → `process_context_switch` → `iret` ke proses berikutnya.

8. **Apa yang terjadi saat `exec hello`?**
   Shell `resolve_executable` via PATH → syscall 11 → kernel `resolve_exec_path` + `process_create_user_process_args` (page dir baru, load EXT2, stack argc/argv, PCB READY) → shell dapat PID; IRQ0 berikutnya menjalankan hello; selesai → syscall 10 → `process_destroy`.

9. **Jelaskan endianness dan buktinya di proyek.** x86 little-endian; contoh: multiboot magic `0x1BADB002` tersimpan `02 B0 AD 1B`; GDT descriptor `0x9A` terlihat `9ACF`.

10. **Apa bedanya MMIO dan PMIO di proyek?** MMIO: framebuffer `0xC00B8000` (tulis memory). PMIO: kursor (`0x3D4/5`), PIC (`0x20/0xA0`), keyboard (`0x60`), ATA (`0x1F0`), PIT (`0x40/0x43`), CMOS (`0x70/0x71`) — lewat `in`/`out`.

---

## 8.2 Ch0 (Toolchain, Kernel, GDT)

1. Kenapa kernel dimuat di 1 MiB? Area bawah dipakai BIOS/EBDA/MMIO.
2. Apa fungsi `.multiboot` dan magic `0x1BADB002`? Tanda agar GRUB mau memuat kernel.
3. Kenapa butuh far jump setelah set `cr0.PE`? `cs` hanya bisa diganti far jump/iret.
4. Null descriptor itu apa dan kenapa ada? Konvensi Intel; selector 0 harus invalid.
5. Apa guna `packed` pada struct descriptor? Layout harus presisi 8 byte sesuai hardware.
6. Apa itu VMA/LMA di linker final? VMA = alamat saat jalan (`0xC0100000`), LMA = alamat saat dimuat (`0x00100000`).

## 8.3 Ch1 (Framebuffer, Interrupt, Keyboard)

1. Format framebuffer text mode? 2 byte/sel: ASCII + atribut `(bg<<4)|fg`.
2. Kenapa `in()` harus `volatile`? Pembacaan hardware bisa berubah; tanpa itu compiler boleh mengoptimasi.
3. PIC remap: ICW1-4 masing-masing untuk apa? Init, offset vektor, cascade master/slave, mode 8086.
4. Kenapa semua IRQ harus di-ACK? Tanpa EOI, PIC memblokir IRQ berikutnya.
5. Kenapa break scancode diabaikan? Bit 7 menandai tombol dilepas; kita hanya butuh make.
6. Kenapa `framebuffer_scroll` pakai `memmove`? Area sumber & tujuan tumpang tindih.
7. Kenapa baca port 0x60 wajib tiap IRQ1? Buffer internal keyboard controller penuh jika tidak dibaca.

## 8.4 Ch2 (Disk & EXT2)

1. ATA PIO: port penting dan status penting? `0x1F0` data, `0x1F2` count, `0x1F3-5` LBA, `0x1F6` drive+LBA24, `0x1F7` status/cmd; BSY=0x80, RDY=0x40, DRQ=0x08.
2. Urutan operasi baca blok? busy-wait → set drive+LBA → command 0x20 → per blok tunggu DRQ → baca 256 word.
3. Struktur disk EXT2 IF2130? blok 0 signature, 1 superblock, 2 BGD, bitmap, inode table, data; 8 grup × 1024 blok.
4. Kenapa `i_block[15]`? 12 direct + 1 single + 1 double + 1 triple indirect.
5. Bagaimana file besar 200 KB ditangani? Direct → single → double indirect; diuji via inserter.
6. Apa isi dirent dan kenapa `rec_len`? `{inode, rec_len, name_len, file_type, nama}`; rec_len = offset ke entri berikutnya (bisa memuat entri kosong).
7. Kenapa `read` mengizinkan buf=NULL? Program user pertama dimuat ke VA 0.
8. Kenapa `create_ext2` menandai blok reserved di bitmap? Blok 0-2 (signature/superblock/BGD) + bitmap + inode table tidak boleh dialokasikan untuk data.

## 8.5 Ch3 (Paging, User Mode, Syscall, Shell)

1. Kenapa higher half? Isolasi kernel vs ruang user per proses.
2. Gambar translasi alamat 32-bit! `virt[31:22]` → indeks page directory; `[21:12]` → indeks page table; `[11:0]` → offset dalam frame.
3. Recursive mapping: bagaimana `0xFFC00000` bekerja? PDE[1023] → dir sendiri; alamat itu dibaca CPU sebagai PDE[1023] + PTE[0]; `0xFFC00000 + i*0x1000` = page table ke-i.
4. Kapan `invlpg` dipanggil? Setelah mengubah PTE/PDE dan saat menghapus mapping identitas.
5. Frame allocator: kenapa frame 0–1023 tidak dipakai? 4 MiB pertama untuk kernel/GRUB/MMIO.
6. TSS untuk apa dan di mana `esp0` diisi? Perpindahan ring 3→0 butuh stack kernel; diisi `set_tss_kernel_current_stack()` dari `ebp + 8`.
7. Kenapa syscall gate DPL 3? `int 0x30` dari user; DPL 0 → #GP.
8. Bagaimana kernel memvalidasi pointer user? `is_user_pointer()` (`< 0xC0000000`, bukan NULL) + `is_valid_fs_request`.
9. Kenapa `main` menerima `argc/argv` otomatis? `build_user_stack` menyusun stack cdecl sebelum proses dijalankan.
10. Kenapa user program di-objcopy flat? Kernel memuat file mentah dari EXT2 ke VA 0; tidak ada loader ELF di OS ini.

## 8.6 Ch4 (Process, Scheduler, Multitasking)

1. Apa isi `struct Context` dan kenapa field `page_directory_virtual_addr` ada? Register + eip + eflags + CR3; CR3 harus ikut berganti saat switch.
2. Langkah `process_create_user_process`? Cek batas → snapshot nama/argv → ukuran dari inode → hitung frame → page dir baru → alokasi kode+stack → ganti CR3 → load EXT2 → init context (segmen 0x23, eip 0, eflags IF) → build stack → restore CR3 → READY.
3. Kenapa context switch assembly? `eip/cs/eflags/esp/ss` hanya bisa dipulihkan via `iret`; register lain via `mov`.
4. Kenapa `edx` dimuat terakhir di context-switch.s? Sebelumnya dipakai sebagai pointer context.
5. PIT: hitung divisor untuk 1 ms! `1193182/1000 ≈ 1193`; mode square wave channel 0.
6. Apa yang terjadi kalau tidak ada proses READY? Idle `hlt` sampai IRQ berikutnya; pada IRQ0 idle tidak melakukan switch (mencegah rekursi).
7. Kenapa `scheduler_switch_to_next_process` `noreturn`? Karena tidak kembali ke pemanggil; ia `iret` ke proses lain.
8. Bagaimana proses berakhir? `crt0` → syscall 10 → `process_destroy` → `scheduler_switch_to_next_process`.
9. Kenapa `paging_use_page_directory` dipanggil sebelum switch? Stack/context proses tujuan hanya valid di address space-nya.
10. Kenapa TSS di-set setelah GDT? Descriptor TSS menunjuk `_interrupt_tss_entry`; `ltr` butuh selector GDT valid.

## 8.7 Bonus

1. PATH disimpan di mana dan kapan dibaca? `/path.cfg`; `path_save()` saat export, `path_load()` saat shell mulai.
2. Bedanya builtin vs terpisah? State/address space; builtin untuk perintah yang butuh cwd/buffer shell.
3. Kenapa recursive cp/rm perlu? Struktur direktori bertingkat; delete menolak folder tak kosong.
4. Kenapa butuh shift map? `|`, `&`, `/`, huruf besar.

---

## 8.8 Pertanyaan "Jebakan" dan Cara Menjawabnya

1. **"Kenapa pakai template, bukan bikin dari nol?"** Template menyediakan toolchain/konvensi; yang dinilai adalah implementasi subsistem (GDT/IDT/EXT2/paging/proses) — bisa ditunjukkan di `src/`.
2. **"Ini tugas kalian atau hasil AI?"** (Jujur) Book/Hub tidak melarang AI; arahkan jawaban ke pemahaman: jelaskan alur kode, tunjukkan letak bug yang pernah diperbaiki (mis. `GROUPS_COUNT` kurang tanda kurung, `CPURegister.stack` salah urutan, PDE[1023] hilang di kit), dan cara mendebug-nya.
3. **"Kenapa `PROCESS_PAGE_FRAME_COUNT_MAX` 512, bukan 8 seperti kit?"** Shell + BSS butuh >8 frame; 8 membuat `process_create` selalu gagal.
4. **"Kenapa root inode 1, bukan 2 seperti Book?"** Desain `create_ext2` kita mengalokasikan root sebagai inode pertama; `make insert-shell` menyesuaikan (parent 1). Book memakai konvensi lain.
5. **"Kenapa `initialize_filesystem_ext2` tidak memvalidasi magic superblock?"** Sesuai Book cukup cek signature blok 0; magic tetap ditulis di superblock saat create (bisa ditingkatkan).
6. **"Kenapa `exec` tidak menunggu?"** Implementasi saat ini background; tidak ada `wait`/reaping (bukan zombie karena PCB di-free saat exit).
7. **"Kenapa triple fault bisa terjadi?"** GDT/IDT/paging salah, mis. descriptor tidak `packed`, PDE tidak 4 KiB-aligned, mapping recursive hilang.
8. **"Bagaimana membuktikan tidak ada kebocoran frame?"** `page_frame_map` + `free_page_frame_count` disesuaikan di setiap allocate/free; jalankan `exec`/`kill` berulang dan `ps` tetap bersih.

---

## 8.9 Daftar Hafalan Minimal (kalau waktu mepet)

| Topik | Yang wajib ada di kepala |
|---|---|
| Boot | BIOS→GRUB→`loader`→`kernel_setup`; multiboot magic; VMA/LMA |
| GDT | 8 byte descriptor; selector = indeks×8+RPL; code 0x08/0x9A, data 0x10/0x92, user 0x1B/0x23, TSS 0x28 |
| Interrupt | PIC remap 0x20/0x28; IDT gate 0x8E; stub → `main_interrupt_handler`; ACK wajib |
| Keyboard | port 0x60; make/break; map scancode; buffer + ACK di ISR |
| Disk | port 0x1F0-0x1F7; BSY/RDY/DRQ; LBA28; 256 word/blok |
| EXT2 | blok 0/1/2; bitmap; inode 64B; 12 direct + indirect; dirent `rec_len` |
| Paging | 10/10/12; PDE[0x300] kernel, PDE[1023] recursive; frame 4 KiB; `invlpg` |
| User mode | TSS esp0; user cs/ss 0x1B/0x23; `iret`; syscall `int 0x30` DPL 3 |
| Proses | PCB: metadata+context+memory; Context: cpu+eip+eflags+CR3 |
| Scheduler | PIT 1000 Hz; round-robin; save→pilih→CR3→context switch |
| Syscall | eax nomor; 0-3 FS, 4 getchar, 6 puts, 10 exit, 11 exec, 12 kill, 13 ps, 14 time |

---

## 8.10 Latihan Alur (tutup panduan, jawab lisan)

1. Sebutkan 10 langkah dari power-on sampai prompt shell muncul.
2. Tuliskan urutan yang terjadi saat `mkdir test` ditekan sampai folder muncul di `ls`.
3. Jelaskan apa yang terjadi pada IRQ0 saat `clock` sedang berjalan dan shell menunggu input.
4. Jelaskan kenapa `cat readme.txt` tidak bisa membaca `/bin/hello` tanpa path.
5. Gambar isi memori untuk alamat virtual `0xC00B8000` sampai byte framebuffer-nya.
6. Sebutkan tiga bug kit yang ditemukan dan diperbaiki di proyek ini beserta dampaknya.

Selesai. Jika keenamnya bisa Anda jawab tanpa membuka file, Anda siap demo.
