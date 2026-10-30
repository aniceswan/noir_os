# Peta Belajar mbOSok — Dari Nol sampai Milestone 4 + Bonus

Dokumen ini adalah pintu masuk. Isinya: cara memakai panduan, peta materi, peta file/kode, dan checklist demo. Panduan ditulis untuk **memahami setiap baris kode** yang ada di repo `mbOSok_noir`, dalam urutan pengembangan yang sebenarnya.

---

## 1. Cara Pakai Panduan Ini

1. Buka dua jendela: satu untuk file panduan (`belajar/`), satu untuk kode sumber (`src/`).
2. Baca bab panduan secara berurutan. Setiap bab punya struktur:
   - **Konsep** — teori singkat yang wajib dipahami sebelum melihat kode.
   - **Alur** — urutan eksekusi dari titik masuk sampai selesai.
   - **Bedah kode** — penjelasan **per baris / per blok** file yang relevan.
   - **Coba sendiri** — langkah praktis (build, jalankan, debug).
   - **Tanya-jawab demo** — pertanyaan yang mungkin muncul saat asistensi/demo.
3. Jangan hafal kode. Hafalkan **alasan** di balik tiap baris. Kalau lupa detail, ingat alurnya lalu cari fungsinya.

> Aturan emas demo: kalau ditanya "kenapa begini?", jawab dengan urutan sebab-akibat hardware → kernel → kode, bukan "karena template".

---

## 2. Peta Milestone ↔ Bab ↔ Bukti

| Milestone | Isi | Tag rilis | Bab panduan |
|---|---|---|---|
| M0 | Toolchain, C kernel, linker, GRUB, Makefile, GDT, protected mode | `v0.1.0` | `02-ch0-*` |
| M1 | Framebuffer (MMIO), PIC remap, IDT, ISR, keyboard driver | `v0.1.1` | `03-ch1-*` |
| M2 | ATA PIO disk driver, EXT2 IF2130 Edition, CRUD, host inserter | `v0.2.x` | `04-ch2-*` |
| M3 | Paging (higher-half, recursive), frame allocator, TSS/user mode, syscall, shell | `v0.3.x` | `05-ch3-*` |
| M4 | Process/PCB, scheduler round-robin, context switch, PIT, exec/ps/kill, clock | `v1.x.x` | `06-ch4-*` |
| Bonus | Environment variables (PATH), separate shell commands, folder cp/rm, Shift | — | `07-bonus-*` |

Catatan: repo `repo_utama/mbOSok` (yang dipush) berisi M0–M1 dan riwayat commit bertahap + tag `v0.1.0`/`v0.1.1`. Repo `mbOSok_noir` berisi versi lengkap sampai M4 + bonus. **Kode yang dibedah di panduan = `mbOSok_noir`**, dan setiap kali ada perbedaan penting antar milestone, saya tuliskan.

---

## 3. Peta File Kode (urut dependency)

Urutan ini adalah urutan "dari nol" — file paling awal dulu:

```
src/menu.lst                     # GRUB: cara memuat kernel
src/linker.ld                    # tata letak memori kernel (VMA/LMA)
src/kernel-entrypoint.s          # titik masuk: paging bootstrap, load GDT, panggil kernel C
src/kernel.c                     # kernel_setup(): orkestrasi seluruh inisialisasi

src/header/cpu/gdt.h, src/cpu/gdt.c               # segmentasi & descriptor
src/header/cpu/portio.h, src/cpu/portio.c         # in/out ke perangkat (PMIO)
src/header/text/framebuffer.h, src/text/framebuffer.c  # output layar (MMIO 0xB8000)
src/header/cpu/interrupt.h, src/cpu/idt.h,
src/cpu/idt.c, src/cpu/intsetup.s, src/cpu/interrupt.c # interrupt, IDT, PIC, handler
src/header/driver/keyboard.h, src/driver/keyboard.c    # input keyboard
src/header/filesystem/disk.h, src/filesystem/disk.c    # baca/tulis blok disk (ATA PIO)
src/header/filesystem/ext2.h, src/filesystem/ext2.c    # filesystem EXT2 IF2130
src/header/memory/paging.h, src/memory/paging.c         # virtual memory, frame allocator
src/header/process/process.h, src/process/process.c     # proses, PCB, alamat virtual per proses
src/header/process/scheduler.h, src/process/scheduler.c # penjadwal, PIT
src/process/context-switch.s                            # ganti konteks (assembly)
src/header/driver/cmos.h, src/driver/cmos.c             # jam RTC (untuk `clock`)
src/header/user/syscall.h                               # nomor syscall bersama kernel & user
src/user/crt0.s, src/user/user-linker.ld, src/user/shell.c, src/user/clock.c, hello.c, echo.c
src/external/external-inserter.c                        # alat host untuk isi storage.bin
Makefile                                                # otomasi build semua di atas
```

---

## 4. Alur Besar Sistem (yang akan selalu ditanya)

```
Power ON
  └─ BIOS/UEFI: POST, cari boot device (MBR/GPT) → load GRUB (grub1, El Torito)
       └─ GRUB baca menu.lst → load /boot/kernel (ELF) + multiboot header
            └─ kernel-entrypoint.s : loader
                 ├─ siapkan page directory + page table awal
                 ├─ enable paging (cr0.PG) → lompat ke higher half (0xC0100000+)
                 ├─ load GDT (lgdt) + jauh ke cs=0x8 (protected mode, ring 0)
                 └─ panggil kernel_setup()  [C]
                      ├─ inisialisasi TSS, IDT, PIC, keyboard
                      ├─ inisialisasi EXT2 dari disk
                      ├─ bikin proses pertama (shell) dengan page directory sendiri
                      └─ scheduler.start → iret ke ring 3 (user mode)
                           └─ shell jalan; syscall `int 0x30` masuk kernel
                                └─ IRQ0 timer tiap 1 ms → context switch antar proses
```

---

## 5. Checklist Demo per Milestone

**M0**
- [ ] Jelaskan isi `menu.lst` dan bagaimana GRUB memuat kernel.
- [ ] Jelaskan `linker.ld`: kenapa `0x00100000`, apa itu `.multiboot`, `.text`, `.bss`.
- [ ] Jelaskan `load_gdt` baris demi baris (`lgdt`, bit PE di `cr0`, far jump, set `ds/es/ss`).
- [ ] Jelaskan `SegmentDescriptor`: limit, base, type, S, DPL, P, G, D/B.
- [ ] Tunjukkan GDT di memori dengan `objdump`/breakpoint gdb.

**M1**
- [ ] Jelaskan kenapa framebuffer di `0xB8000` (MMIO) dan format 2 byte per karakter.
- [ ] Jelaskan `in`/`out` (PMIO) dan kenapa kursor pakai port `0x3D4/0x3D5`.
- [ ] Jelaskan PIC remap (ICW1–ICW4) dan kenapa IRQ digeser ke `0x20`.
- [ ] Jelaskan `IDTGate`, DPL, bit P, dan `lidt`/`sti`.
- [ ] Jelaskan alur IRQ keyboard → `intsetup.s` → `main_interrupt_handler` → `keyboard_isr` → `pic_ack`.

**M2**
- [ ] Jelaskan ATA PIO: port `0x1F0`–`0x1F7`, BSY/DRQ, LBA28, baca 256 word per blok.
- [ ] Gambar struktur EXT2 IF2130: boot sector, superblock (blok 1), BGD (blok 2), bitmap, inode table, data.
- [ ] Jelaskan dirent (`inode`, `rec_len`, `name_len`, `file_type`) dan cara traverse.
- [ ] Jelaskan `read`, `write`, `delete` termasuk alokasi blok tidak langsung (indirect).
- [ ] Tunjukkan `storage.bin` di hex editor dan host `inserter`.

**M3**
- [ ] Jelaskan kenapa kernel dipindah ke higher half (`0xC0000000`) dan bedanya VMA/LMA.
- [ ] Jelaskan paging 2 tingkat: page directory → page table → frame 4 KiB.
- [ ] Jelaskan recursive page mapping (`PDE[1023]`) dan alamat ajaib `0xFFC00000`.
- [ ] Jelaskan TSS, GDT user (DPL 3), dan perpindahan ring 0 → ring 3 (iret).
- [ ] Jelaskan syscall `int 0x30`: nomor di `eax`, argumen `ebx/ecx/edx`, gate DPL 3.
- [ ] Jelaskan alur `shell`: `getchar` syscall → parse baris → panggil syscall FS.

**M4**
- [ ] Gambar `ProcessControlBlock`: metadata (pid, state, nama), context (register), page directory.
- [ ] Jelaskan `process_create_user_process` langkah demi langkah.
- [ ] Jelaskan `context-switch.s`: kenapa harus assembly, bagaimana `iret` dipakai.
- [ ] Jelaskan PIT (divisor), IRQ0, dan mengapa `scheduler_switch_to_next_process` tidak kembali.
- [ ] Jelaskan `ps`, `kill`, `exec` dari sisi kernel dan user.
- [ ] Jelaskan bonus PATH: resolusi perintah eksternal + persistensi `/path.cfg`.

---

## 6. Cara Build, Jalankan, dan Debug (ringkas)

```bash
# di mbOSok_noir
make prepare        # clean build + buat disk + masukkan shell & program /bin
make run            # QEMU (mode debug: -s -S, gdb server di :1234), disk terpasang
```

Untuk belajar, biasakan:
- `objdump -d bin/kernel | less` untuk melihat hasil compile.
- `nm bin/kernel` untuk alamat simbol (mis. `global_descriptor_table`, `_idt_idtr`).
- QEMU + `gdb` (`.vscode/launch.json` sudah menyiapkan `target remote :1234`).

---

## 7. Urutan Membaca Panduan

1. `01-konsep-dasar.md` — hardware & OS dari nol (wajib, jangan skip).
2. `02-ch0-toolchain-kernel-gdt.md`
3. `03-ch1-framebuffer-interrupt-keyboard.md`
4. `04-ch2-disk-ext2.md`
5. `05-ch3-paging-usermode-shell.md`
6. `06-ch4-process-scheduler.md`
7. `07-bonus-env-separate-commands.md`
8. `08-demo-qna.md` — kumpulan tanya-jawab demo/ujian.
