# PANDUAN BELAJAR mbOSok — LENGKAP (Satu File)

Dokumen ini adalah gabungan seluruh bab panduan, disusun berurutan dari nol sampai Milestone 4 + bonus.
Dibuat otomatis dari file-file di folder `belajar/`.

## Daftar Isi

1. **Peta Belajar** — `00-peta-belajar.md`
2. **Bab 1 — Konsep Dasar** — `01-konsep-dasar.md`
3. **Bab 2 — Chapter 0** — `02-ch0-toolchain-kernel-gdt.md`
4. **Bab 3 — Chapter 1** — `03-ch1-framebuffer-interrupt-keyboard.md`
5. **Bab 4 — Chapter 2** — `04-ch2-disk-ext2.md`
6. **Bab 5 — Chapter 3** — `05-ch3-paging-usermode-shell.md`
7. **Bab 6 — Chapter 4** — `06-ch4-process-scheduler.md`
8. **Bab 7 — Bonus** — `07-bonus-env-separate-commands.md`
9. **Bab 8 — Demo Q&A** — `08-demo-qna.md`

---



<!-- ==================== 00-peta-belajar.md ==================== -->

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



---



<!-- ==================== 01-konsep-dasar.md ==================== -->

# Bab 1 — Konsep Dasar: Hardware, OS, dan Alat

Tujuan bab ini: Anda paham "panggung" tempat semua kode kita berperan. Setelah bab ini, istilah seperti *protected mode*, *interrupt*, *paging*, *inode*, dan *syscall* akan terasa konkret, bukan hafalan.

---

## 1.1 Komputer dan Sistem Operasi

Sebuah komputer pada dasarnya hanya:

```
        +--------+         bus (address, data, control)         +-----------+
        |  CPU   |<============================================>|  Memory   |
        +--------+                                              +-----------+
             ^                                                        ^
             |                                                        |
             v                                                        v
        +-----------------------------------------------+   +----------------+
        | Perangkat: keyboard, disk, timer, layar (MMIO)|   | ROM/BIOS, dsb. |
        +-----------------------------------------------+   +----------------+
```

CPU hanya bisa: mengambil instruksi, menghitung, membaca/menulis memory atau port. Semua "kecerdasan" lain (menampilkan layar, membaca keyboard, menyimpan file) harus **diprogram**.

- **Kernel** = program pertama yang berjalan dengan hak penuh (ring 0), mengatur memory, perangkat, dan proses.
- **User program** = program biasa (ring 3) yang minta layanan kernel lewat **syscall**.
- **Sistem operasi** = kernel + layanan (filesystem, shell, driver).

Analogi: kernel itu petugas gedung yang punya kunci semua ruangan; user program itu tamu yang harus minta izin lewat loket (syscall).

---

## 1.2 CPU x86 32-bit dan Register

### Register umum (general purpose)

| Register | Kegunaan umum | Di proyek kita |
|---|---|---|
| `eax` | akumulator, hasil fungsi | nomor syscall, parameter |
| `ebx`, `ecx`, `edx` | serbaguna | argumen syscall / fungsi |
| `esi`, `edi` | index/pointer | pointer buffer |
| `esp` | stack pointer (selalu ke bawah) | stack kernel/user |
| `ebp` | base pointer frame fungsi | frame `main_interrupt_handler` |

### Register khusus

| Register | Arti | Dipakai di |
|---|---|---|
| `eip` | alamat instruksi berikutnya | frame interrupt, context switch |
| `eflags` | status (ZF, IF, dll) | iret, test proses |
| `cs` | segment kode (ring) | protected mode, user mode |
| `ds/es/fs/gs/ss` | segment data | interupsi, user mode |
| `cr0` | kontrol (PG, PE) | aktifkan protected mode & paging |
| `cr3` | alamat page directory | ganti address space proses |
| `cr4` | fitur (PSE dll) | pilih 4 KiB page |

### Instruksi penting yang wajib dikenali

| Instruksi | Arti | Muncul di |
|---|---|---|
| `mov` | salin nilai | mana-mana |
| `push`/`pop` | simpan/ambil dari stack | intsetup.s, context-switch.s |
| `call`/`ret` | panggil/kembali fungsi | kernel-entrypoint.s |
| `lgdt`/`lidt` | muat GDT/IDT | `load_gdt`, `initialize_idt` |
| `cli`/`sti` | matikan/nyalakan interrupt flag | kritis saat setup |
| `in`/`out` | baca/tulis port I/O | portio.c, PIC, ATA |
| `int n` | panggil interrupt/exception/syscall | `int 0x30`, `int $0x4` |
| `iret` | kembali dari interrupt + restore ring | intsetup.s, context-switch.s |
| `invlpg` | buang 1 entri TLB | paging.c |
| `ltr` | muat Task Register (TSS) | set_tss_register |

**Little-endian**: x86 menyimpan angka besar di byte belakang. Nilai `0x1BADB002` di memori = `02 B0 AD 1B`. Ini sebabnya dump GDT terlihat "terbalik".

---

## 1.3 Alamat Memori: Fisik, Linear, Virtual

Intel 32-bit punya rantai penerjemahan alamat:

```
Alamat logis (segment:offset)
   │  segmentasi (GDT: base + limit)
   v
Alamat linear
   │  paging (page directory → page table → frame)
   v
Alamat fisik → RAM/perangkat
```

- **Segmentasi** memetakan `selector:offset` → linear. Di proyek ini semua segmen "flat" (base 0, limit penuh), jadi linear ≈ offset. Yang tetap berguna dari segmentasi: **proteksi ring** (DPL) dan TSS.
- **Paging** memetakan halaman 4 KiB. Ini yang memberi tiap proses ilusi punya memory sendiri.
- **MMIO (memory-mapped I/O)**: perangkat "muncul" di alamat tertentu. Contoh: text framebuffer VGA di `0xB8000` (atau `0xC00B8000` setelah kernel dipindah ke higher half).
- **PMIO (port-mapped I/O)**: perangkat diakses lewat instruksi `in/out` dengan nomor port. Contoh: kursor `0x3D4/0x3D5`, PIC `0x20/0xA0`, ATA `0x1F0`.

---

## 1.4 Mode CPU dan Ring

| Mode | Ciri | Kapan |
|---|---|---|
| Real mode | 16-bit, alamat < 1 MiB, tanpa proteksi | saat BIOS/GRUB awal |
| Protected mode | 32-bit, segmentasi + ring + paging | sejak `load_gdt` kita |
| Long mode | 64-bit | tidak dipakai di proyek |

- **Ring 0** = kernel (boleh semua instruksi), **Ring 3** = user.
- Perpindahan ring 3→0 hanya lewat **interrupt/syscall** (hardware memverifikasi lewat IDT/GDT).
- Perpindahan 0→3 hanya lewat `iret` dengan `cs` ber-DPL 3.

---

## 1.5 Booting: dari Tombol Power ke `kernel_setup`

```
1. Power ON  → CPU mulai di alamat reset, jalankan firmware (BIOS/UEFI).
2. POST      → cek hardware.
3. Firmware cari boot device pertama (sesuai urutan boot).
4. Boot sector dibaca ke RAM.
   - MBR (legacy) : sektor pertama disk, 512 byte.
   - GPT (modern) : ada "protective MBR" lalu header GPT.
5. Bootloader (GRUB) berjalan, membaca konfigurasi (menu.lst).
6. GRUB memuat file kernel dari disk (ELF) dan menyiapkan:
   - memori, mode yang sesuai, lalu lompat ke entry point (loader).
7. Kernel berjalan. Di sinilah kode kita mulai.
```

**Multiboot header**: agar GRUB mau memuat kernel kita, kernel harus punya "tanda" (magic `0x1BADB002`) di 8 KiB pertama file. Itulah tiga baris `dd` di `kernel-entrypoint.s`.

**ELF**: format file executable. Bagian pentingnya: *section* (`.text`, `.rodata`, `.data`, `.bss`) dan *header* yang memberi tahu linker/loader ke mana isinya diletakkan. Linker script (`linker.ld`) yang menentukan alamat (`0x00100000` untuk M0, lalu VMA higher-half `0xC0100000` mulai Ch3).

---

## 1.6 Interrupt: Jantung Sistem Interaktif

CPU hanya bisa mengerjakan satu hal; interrupt membuatnya bisa "dipanggil" dunia luar.

| Jenis | Contoh | Sumber |
|---|---|---|
| Hardware interrupt (IRQ) | keyboard (IRQ1), timer (IRQ0), disk (IRQ14) | PIC/APIC |
| Exception | #DE (bagi nol), #PF (page fault), #GP | instruksi CPU |
| Software interrupt | syscall `int 0x30` | program |

Alur hardware IRQ:

```
perangkat → PIC → CPU
                    ├─ simpan eflags, cs, eip (+ss,esp kalau ganti ring) ke stack
                    ├─ baca IDT[vektor] → alamat ISR
                    └─ lompat ke ISR (kode assembly kita) → panggil handler C
```

**PIC 8259**: chip pengelola IRQ. Default IRQ 0–7 bertabrakan dengan exception CPU 0–7, maka kita **remap** IRQ ke `0x20` ke atas. Di proyek ini: IRQ0 (timer) = `0x20`, IRQ1 (keyboard) = `0x21`.

**IDT**: tabel 256 entri; tiap entri = alamat handler + segment + DPL. `intsetup.s` menyediakan 64 stub (`isr_stub_table`), `idt.c` mengisi IDT dengan alamat-alamat itu.

**Aturan penting**: semua IRQ yang dikirim PIC **wajib di-ACK** (kirim EOI `0x20` ke port PIC). Kalau tidak, IRQ berikutnya disandera.

---

## 1.7 Penyimpanan: Block, ATA, Filesystem

- **Block device**: disk dibaca per blok 512 byte, dialamatkan dengan **LBA** (nomor blok linear).
- **ATA PIO**: cara paling sederhana bicara IDE/SATA disk: kirim nomor LBA & jumlah blok ke port `0x1F0`–`0x1F7`, tunggu bit **BSY** hilang dan **DRQ** menyala, lalu baca 256 word (2 byte) per blok lewat port data.
- **Filesystem**: memberi struktur pada blok. Proyek memakai **EXT2 "IF2130 Edition"** dengan bagian:
  - **boot sector** (blok 0) — dipakai menandai filesystem (signature).
  - **superblock** (blok 1) — metadata global: jumlah inode/blok, magic `0xEF53`, dll.
  - **BGD (block group descriptor)** (blok 2) — lokasi bitmap dan tabel inode.
  - **block bitmap & inode bitmap** — penanda blok/inode kosong.
  - **inode table** — array inode; inode = metadata file + 15 pointer blok data.
  - **data blocks** — isi direktori (daftar `EXT2DirectoryEntry`) atau isi file.
- **Inode** menyimpan `i_mode` (file/folder), `i_size`, `i_blocks`, dan `i_block[15]`: 12 direct, 1 single indirect, 1 double indirect, 1 triple (tidak dipakai).

---

## 1.8 Proses dan Scheduling

- **Program** = file di disk. **Proses** = program yang sedang jalan + seluruh "status"-nya (register, stack, address space).
- **PCB (Process Control Block)** menyimpan: pid, state, nama, konteks CPU, dan page directory.
- **Context switch** = menyimpan semua register proses lama dan memuat register proses baru. Harus assembly karena `esp`, `eip`, `cs`, `eflags` tidak bisa disetel langsung dari C.
- **PIT (Programmable Interval Timer)**: pembangkit IRQ0 periodik. Kita atur agar IRQ0 datang 1000 kali/detik (divisor 1193), sehingga scheduler bisa **preemptive** (menyela proses tiap 1 ms).
- **Round-robin**: pilih proses RUNNING/READY berikutnya secara bergiliran; adil dan sederhana.

---

## 1.9 Toolchain: Apa yang Sebenarnya Terjadi Saat `make build`

1. **nasm** merakit `*.s` → objek `.o` (ELF32).
2. **gcc -m32 -ffreestanding -nostdlib ...** mengompilasi `*.c` → objek. Karena *freestanding*, kita tidak boleh pakai libc; kita sediakan sendiri `memset`, `memcpy`, dll (`src/stdlib/string.c`).
3. **ld** menyatukan semua `.o` sesuai `linker.ld` → `bin/kernel` (ELF32-i386).
4. **genisoimage** membungkus `bin/kernel` + GRUB (`other/grub1` + `menu.lst`) menjadi ISO bootable (El Torito).
5. **qemu-system-i386** menjalankan ISO itu sebagai komputer virtual.

Untuk program user (shell, clock, hello, echo): dikompilasi `-m32 -fno-pie`, di-link dengan `user-linker.ld` ke alamat `0`, lalu di-`objcopy` jadi binary pipih (flat) agar mudah dimuat kernel. Alat host **inserter** menulis binary itu ke dalam `storage.bin` sesuai format EXT2.

---

## 1.10 Daftar Istilah Cepat

| Istilah | Arti singkat |
|---|---|
| VMA / LMA | alamat saat dijalankan / alamat saat dimuat |
| Higher half | kernel dipetakan di paruh atas ruang alamat (`≥ 0xC0000000`) |
| Recursive mapping | trik memetakan page directory ke dirinya sendiri agar bisa diubah dari kode |
| Frame | halaman fisik 4 KiB |
| ISR | Interrupt Service Routine |
| EOI/ACK | pemberitahuan "interrupt selesai" ke PIC |
| Dirent | entri direktori (nama + inode) |
| Syscall | pintu resmi user → kernel |
| Preemptif | penjadwal bisa menyela tanpa izin proses |
| Freestanding | lingkungan C tanpa libc/OS di bawahnya |

---

Bab berikutnya: `02-ch0-toolchain-kernel-gdt.md` — kita bedah `Makefile`, `menu.lst`, `linker.ld`, `kernel-entrypoint.s`, `gdt.h`, `gdt.c`, dan `kernel.c` **setiap baris**, plus cara memverifikasi GDT di QEMU/gdb.



---



<!-- ==================== 02-ch0-toolchain-kernel-gdt.md ==================== -->

# Bab 2 — Chapter 0: Toolchain, Kernel, dan GDT

Bab ini membedah **setiap baris** dari file-file yang membuat OS pertama kali bisa boot:

`Makefile` → `menu.lst` → `linker.ld` → `kernel-entrypoint.s` → `gdt.h` → `gdt.c` → `kernel.c`.

Catatan penting: kode di `mbOSok_noir` adalah versi **final (sampai M4)**. Untuk Ch0, ada dua "wujud":
- **Wujud M0** (di repo yang di-commit, `repo_utama/mbOSok`): kernel di alamat fisik `0x00100000`, tanpa paging, tanpa user mode.
- **Wujud final** (di `mbOSok_noir`): kernel di higher half `0xC0100000`, sudah pakai paging, TSS, user mode.

Saya jelaskan keduanya supaya Anda paham **perjalanan** kodenya, bukan hanya hasil akhir. Setiap bagian saya tandai [M0] atau [FINAL].

---

## 2.1 `Makefile` — otomasi build dari nol

File: `Makefile` (final). Bagian per bagian:

```make
ASM           = nasm
LIN           = ld
CC            = gcc
```
**Variabel tool.** `nasm` untuk assembly, `ld` untuk menyatukan objek, `gcc` untuk C. Ditulis sebagai variabel supaya mudah diganti.

```make
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage
```
Semua hasil build masuk `bin/`. Nama ISO dan disk juga variabel agar konsisten.

```make
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
```
- `-Wall -Wextra -Werror`: semua peringatan jadi error — memaksa kode bersih.
- `-g`: simpan info debug (untuk gdb).
- `-fshort-wchar`: `wchar_t` 2 byte (kebiasaan template).
- `-nostdlib -nostartfiles -nodefaultlibs -ffreestanding`: **jangan** pakai libc/startup OS; kita berdiri sendiri di atas hardware.
- `-fno-stack-protector`: tidak ada canary (butuh runtime libc).
- `-m32`: keluarkan kode 32-bit.
- `-c`: hanya compile ke objek (belum link).
- `-I$(SOURCE_FOLDER)`: agar `#include "header/..."` ketemu.

```make
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386
```
- NASM: hasilkan objek ELF32 + simbol DWARF (debug).
- LD: pakai linker script kita + format x86 32-bit.

```make
ASM_SOURCES   = $(shell find $(SOURCE_FOLDER) -name '*.s')
C_SOURCES     = $(shell find $(SOURCE_FOLDER) -name '*.c')
ASM_OBJECTS   = $(patsubst $(SOURCE_FOLDER)/%.s,$(OUTPUT_FOLDER)/%.o,$(ASM_SOURCES))
C_OBJECTS     = $(patsubst $(SOURCE_FOLDER)/%.c,$(OUTPUT_FOLDER)/%.o,$(C_SOURCES))
OBJECTS       = $(ASM_OBJECTS) $(C_OBJECTS)
```
Auto-discover semua `.c`/`.s` di `src/`, lalu ubah path `src/x/y.c` → `bin/x/y.o`. Ini yang membuat kita tidak perlu menambah baris saat menambah file baru. **Perhatikan**: `src/user/` dikecualikan di file final (program user punya toolchain sendiri) — lihat definisi `C_SOURCES` sebenarnya di bawah ini. Kalau di Makefile final tertulis `find ... -not -path '*/user/*'`, itulah alasannya.

```make
kernel: $(OBJECTS)
	@$(LIN) $(LFLAGS) $(OBJECTS) -o $(OUTPUT_FOLDER)/kernel
```
Target `kernel`: setelah semua objek siap, link jadi `bin/kernel` (ELF32). `@` = jangan echo perintahnya.

```make
$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.s
	@mkdir -p $(dir $@)
	@$(ASM) $(AFLAGS) $< -o $@
$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@
```
**Pola umum** (pattern rule): "untuk setiap target `.o`, build dari file sumber yang sesuai". `$<` = sumber, `$@` = target, `$(dir $@)` = foldernya (dibuat dulu). Inilah TODO Chapter 0 di template: *compile C dengan CFLAGS*.

```make
iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 \
		-A os -input-charset utf8 -quiet -boot-info-table \
		-o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/
```
Target `iso` (TODO kedua Chapter 0):
1. Siapkan struktur folder ISO ala GRUB.
2. Salin kernel + `grub1` (stage2 El Torito) + `menu.lst`.
3. `genisoimage` membuat ISO bootable:
   - `-b boot/grub/grub1`: file boot loader di ISO.
   - `-no-emul-boot`: mode El Torito no-emulation (standar GRUB).
   - `-boot-load-size 4`: muat 4 sektor virtual.
   - `-boot-info-table`: sisipkan tabel info boot.
4. Hapus folder temporer.

```make
disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M
prepare: clean build disk insert-shell insert-programs
```
- `disk`: buat image 4 MiB kosong untuk EXT2 (Ch2).
- `prepare`: dari nol → build ISO → disk baru → masukkan semua program user.

```make
run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@qemu-system-i386 -s -S -cdrom ... -drive file=...,format=raw,if=ide,index=0,media=disk
```
- `-s -S`: buka gdb server di `:1234` dan **pause** di awal (untuk debugging).
- `-drive ... if=ide`: pasang `storage.bin` sebagai disk IDE (nanti dibaca driver ATA).

```make
inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c $(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c -o $(OUTPUT_FOLDER)/inserter
```
Kompilasi **host tool** (jalan di Linux Anda, bukan di OS): kode EXT2 yang sama dipakai untuk menulis file ke `storage.bin`.

---

## 2.2 `menu.lst` — instruksi untuk GRUB

```
default 0
timeout 0
title os
kernel /boot/kernel
```
- `default 0`: pilih entri pertama.
- `timeout 0`: jangan tunggu (langsung boot).
- `title os`: nama entri.
- `kernel /boot/kernel`: muat file ELF ini sebagai kernel, lalu lompat ke entry point yang tertulis di `linker.ld` (`ENTRY(loader)`).

---

## 2.3 `linker.ld` — menentukan letak semua bagian kernel

### [M0] versi awal (dipakai saat commit `v0.1.0`)

```ld
ENTRY(loader)
SECTIONS {
    . = 0x00100000;                  /* kernel dimuat di 1 MiB */
    .multiboot ALIGN(0x1000) : { *(.multiboot) }
    .text      ALIGN(0x1000) : { *(.text) }
    .rodata    ALIGN(0x1000) : { *(.rodata*) }
    .data      ALIGN(0x1000) : { *(.data) }
    .bss       ALIGN(0x1000) : { *(COMMON) *(.bss) }
}
```
- `ENTRY(loader)`: simbol pertama yang dijalankan.
- `. = 0x00100000`: 1 MiB. Kenapa tidak 0? Karena `0x0–0xFFFFF` masih dipakai BIOS/GRUB (lihat gambar memory map di Book).
- Setiap section di-align 4 KiB agar rapi.
- `.multiboot` **harus** di awal (header multiboot wajib ada di 8 KiB pertama file).

### [FINAL] versi higher-half (mulai Chapter 3)

```ld
ENTRY(loader)
SECTIONS {
    . = 0xC0100000;                                     /* VMA */
    _linker_kernel_virtual_addr_start  = .;
    _linker_kernel_physical_addr_start = . - 0xC0000000; /* LMA = 0x00100000 */
    .multiboot ALIGN(0x1000) : AT (ADDR(.multiboot) - 0xC0000000) { *(.multiboot) }
    ...
}
```
- **VMA (virtual memory address)** = alamat saat dijalankan = `0xC0100000`.
- **LMA (load memory address)** = alamat fisik saat dimuat GRUB = `0xC0100000 - 0xC0000000 = 0x00100000`.
- `AT(...)` memberi tahu linker: letakkan isi section di LMA, tapi alamat kode yang dihasilkan memakai VMA.
- `_linker_kernel_*` = variabel simbol yang bisa dibaca kernel C untuk tahu batas kernel.
- `.setup.text` khusus untuk kode bootstrap yang jalan **sebelum** paging aktif; ia dipetakan identitas agar bisa dieksekusi.
- `bin/kernel-entrypoint.o(.bss)` + `_linker_kernel_stack_top`: kira-kira ruang stack kernel; akhir stack diberi label.

**Kenapa dipindah?** Supaya kernel berada di paruh atas; user program bebas memakai paruh bawah (`0x00000000–0xBFFFFFFF`) tanpa bertabrakan. Ini alasan paging di Ch3.

---

## 2.4 `kernel-entrypoint.s` — titik masuk pertama

### [M0] versi awal

```asm
global loader
global load_gdt
extern kernel_setup
KERNEL_STACK_SIZE equ 4096
MAGIC_NUMBER      equ 0x1BADB002
FLAGS             equ 0x0
CHECKSUM          equ -MAGIC_NUMBER

section .bss
align 4
kernel_stack: resb KERNEL_STACK_SIZE

section .multiboot
align 4
    dd MAGIC_NUMBER
    dd FLAGS
    dd CHECKSUM

section .text
loader:
    mov esp, kernel_stack + KERNEL_STACK_SIZE
    call kernel_setup
.loop: jmp .loop
```
Baris demi baris:
- `global loader` — simbol ini ditunjuk `ENTRY(loader)` di linker script.
- `MAGIC_NUMBER 0x1BADB002` dll — header multiboot: GRUB hanya memuat kernel kalau tiga angka ini valid (`magic + checksum + flags == 0`).
- `.bss` + `resb 4096` — cadangkan 4 KiB stack di BSS (BSS tidak menambah ukuran file, hanya memori).
- `mov esp, kernel_stack + KERNEL_STACK_SIZE` — stack tumbuh ke bawah; `esp` diisi alamat **ujung atas**. Sebelum ini, stack belum ada!
- `call kernel_setup` — lompat ke C. Sejak sini kita berkode C.
- `.loop: jmp .loop` — kalau C kembalikan kontrol, berhenti di situ (tidak ada OS untuk kembali).

`load_gdt` (dipakai kernel C):
```asm
load_gdt:
    cli
    mov  eax, [esp+4]   ; argumen 1 (pointer GDTR) sesuai cdecl
    lgdt [eax]          ; muat isi GDTR ke register GDTR CPU
    mov  eax, cr0
    or   eax, 1         ; bit PE (Protection Enable)
    mov  cr0, eax
    jmp  0x8:flush_cs   ; far jump: muat ulang cs dengan selector 0x8
flush_cs:
    mov ax, 10h         ; selector data kernel (0x10)
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret
```
- `cli`: matikan interrupt selama transisi.
- `[esp+4]`: pointer argumen; cdecl = argumen ada di stack.
- `lgdt`: hardware membaca 2+4 byte dari `_gdt_gdtr` (size + alamat).
- set bit 0 `cr0`: masuk protected mode.
- **far jump** `jmp 0x8:flush_cs`: instruksi ini memuat `cs` dengan selector `0x8` (indeks 1 GDT × 8). Wajib far jump karena `cs` tidak bisa di-`mov`.
- `ss, ds, es = 0x10` (data kernel). Kalau GDT salah, far jump ini langsung exception → bootloop. Karena itu ada peringatan di header.

### [FINAL] tambahan di `mbOSok_noir`

1. **Bootstrap paging** di `.setup.text` (dijelaskan detail di Bab 5), urutannya:
   - isi page table 1024 entri → memetakan 0–4 MiB (identitas dan higher-half).
   - isi page directory: `PDE[0]` (identitas, sementara) dan `PDE[0x300]` (higher half `0xC0000000`).
   - `PDE[1023]` = menunjuk page directory itu sendiri (**recursive mapping**, kunci trik paging nanti).
   - set `cr3` = alamat page directory; `cr4 &= ~0x10` (pakai 4 KiB page); `cr0 |= 0x80000000` (aktifkan paging).
   - `lea eax,[loader_virtual]; jmp eax` — lompat ke alamat virtual higher half.
   - hapus mapping identitas `PDE[0]`, `invlpg [0]`, lalu set `esp` ke stack kernel dan `call kernel_setup`.
2. **`set_tss_register`**: `mov ax, 0x28|0; ltr ax` → beri tahu CPU letak TSS (untuk perpindahan ring 3→0).
3. **`kernel_execute_user_program`**: trik `iret` untuk pindah ke ring 3:
   - set `ds/es/fs/gs` ke selector user data `0x20|3`.
   - `push` berurutan: `ss` (0x23), `esp` user, `eflags`, `cs` (0x1B), `eip` (entry).
   - `iret` membaca kelimanya dan berpindah ke user mode. Alamat stack user = `entry + 0x400000 - 4` (puncak 4 MiB pertama).

---

## 2.5 `gdt.h` — mendefinisikan descriptor segment

GDT = array descriptor (8 byte masing-masing). Bitfield `SegmentDescriptor` mengikuti Intel Gambar 3-8:

```c
struct SegmentDescriptor {
    uint16_t segment_low;          // bit 15:0  limit[15:0]
    uint16_t base_low;             // bit 31:16 base[15:0]

    uint8_t base_mid;              // bit 39:32 base[23:16]
    uint8_t type_bit   : 4;        // bit 43:40 tipe segmen
    uint8_t non_system : 1;        // bit 44    S (code/data=1, system=0)
    uint8_t dpl        : 2;        // bit 46:45 privilege level
    uint8_t present    : 1;        // bit 47    P (ada/tidak)

    uint8_t segment_high : 4;      // bit 51:48 limit[19:16]
    uint8_t avl          : 1;      // bit 52
    uint8_t long_mode    : 1;      // bit 53    L (64-bit code)
    uint8_t default_op   : 1;      // bit 54    D/B (32-bit operand)
    uint8_t granularity  : 1;      // bit 55    G (4 KiB)
    uint8_t base_high;             // bit 63:56 base[31:24]
} __attribute__((packed));
```
Kenapa `packed`? Supaya kompiler **tidak** menambah padding; layout harus presisi 8 byte karena hardware yang membacanya.

`struct GlobalDescriptorTable { table[32]; }` — array 32 descriptor (kita pakai 6).
`struct GDTR { uint16_t size; struct GlobalDescriptorTable *address; }` — ukuran = `sizeof(GDT)-1`, plus alamat GDT.

**Selector**: `indeks_di_GDT * 8 + RPL`. Jadi:
- kernel code = indeks 1 → `0x08`; kernel data = 2 → `0x10`
- user code = 3 → `0x18|3 = 0x1B`; user data = 4 → `0x20|3 = 0x23`
- TSS = 5 → `0x28`.

---

## 2.6 `gdt.c` — mengisi GDT

### [M0] tiga descriptor (Null, Kernel Code, Kernel Data)

Nilai penting:
- **Null descriptor** (indeks 0): semua nol. Wajib, agar selector `0` dianggap tidak valid.
- **Kernel code** (indeks 1): `limit=0xFFFFF`, `base=0`, `type=0xA` (code, readable), `S=1`, `DPL=0`, `P=1`, `G=1`, `D/B=1` → byte akses `0x9A`, byte flag `0xCF`.
- **Kernel data** (indeks 2): sama, tapi `type=0x2` (data, writable) → byte akses `0x92`.

`_gdt_gdtr.size = sizeof(GDT) - 1` (konvensi Intel: ukuran disimpan minus 1).

### [FINAL] tambahan
- **User code/data** (indeks 3,4) dengan `dpl=3`.
- **TSS descriptor** (indeks 5): `non_system=0` (system), `type=0x9` (32-bit TSS available), `granularity=0` (byte), `limit = sizeof(struct TSSEntry)`.
- `gdt_install_tss()`: TSS baru diketahui alamatnya saat runtime, jadi descriptor-nya **ditambal**:
```c
uint32_t base = (uint32_t) &_interrupt_tss_entry;      // alamat TSS
table[5].base_low  = base & 0xFFFF;                    // base 15:0
table[5].base_mid  = (base >> 16) & 0xFF;              // base 23:16
table[5].base_high = (base >> 24) & 0xFF;              // base 31:24
```
Inilah alamat "base" yang tidak muat dalam satu field, jadi dipecah 3 bagian di struct.

---

## 2.7 `kernel.c` — orkestrasi

### [M0] versi paling awal (commit 1)
```c
void kernel_setup(void) { while (true); }
```
Sungguh hanya loop. Tujuannya: membuktikan kernel C bisa di-link dan dipanggil dari assembly.

### [M0 setelah GDT]
```c
void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    while (true);
}
```
`&_gdt_gdtr` = pointer ke GDTR global di `gdt.c`. Setelah ini CPU resmi di protected mode dengan segmentasi benar.

### [FINAL] `mbOSok_noir`
```c
load_gdt(&_gdt_gdtr);              // segmentasi + protected mode
pic_remap();                        // geser IRQ ke 0x20 (Bab 3)
initialize_idt();                   // daftarkan handler interrupt (Bab 3)
activate_keyboard_interrupt();      // nyalakan IRQ1 (Bab 3)
framebuffer_clear();                // bersihkan layar (Bab 3)
initialize_filesystem_ext2();       // baca/buat EXT2 dari disk (Bab 4)
gdt_install_tss(); set_tss_register(); // TSS untuk user mode (Bab 5)
// muat file "shell" dari root EXT2 (inode 1), maks 1 MiB
int32_t ret = process_create_user_process(request);
scheduler_init();
scheduler_switch_to_next_process(); // iret ke ring 3 → shell jalan
```
Urutannya tidak boleh sembarangan: GDT → interrupt → output → filesystem → proses. Contoh: `process_create_user_process` butuh EXT2 dan paging; `scheduler` butuh PIT/IDT.

---

## 2.8 Verifikasi Ch0 (yang sebaiknya Anda praktikkan)

```bash
cd mbOSok_noir && make prepare
objdump -s -j .data bin/kernel | grep -A2 "103000"   # lihat byte GDT (alamat bisa beda)
nm -n bin/kernel | grep -E "global_descriptor_table|_gdt_gdtr|loader|kernel_setup"
readelf -S bin/kernel | grep -E "multiboot|setup.text|text|bss"
```
Yang dicek:
- `.multiboot` ada dan magic `02 b0 ad 1b` di 8 KiB pertama file.
- `global_descriptor_table` isinya: `00..` (null), `FFFF0000 009ACF00` (kernel code), `FFFF0000 0092CF00` (kernel data).
- Entry point kernel benar; `_gdt_gdtr` size `0x00FF`.

Di QEMU+gdb: `b kernel_setup`, lihat `info registers` (cr0, cs, ds), `x/8gx &global_descriptor_table`.

---

## 2.9 Tanya-Jawab Demo Ch0

1. **Kenapa kernel di 0x00100000?** Karena `0x0–0xFFFFF` dipakai BIOS/EBDA/MMIO; 1 MiB adalah alamat konvensional setelah area itu.
2. **Apa fungsi CHECKSUM?** Agar `MAGIC + FLAGS + CHECKSUM == 0`; GRUB memvalidasi.
3. **Kenapa harus null descriptor?** Hardware memperlakukan selector 0 invalid; juga konvensi Intel.
4. **Apa bedanya S, DPL, P?** S = jenis (sistem vs code/data), DPL = ring yang boleh, P = segmen ada di memori.
5. **Kenapa `lgdt` pakai pointer, bukan nilai?** Instruksi `lgdt [eax]` membaca 6 byte dari memori (2+4).
6. **Kenapa far jump setelah set PE?** `cs` hanya bisa dimuat lewat far jump/return/iret; tanpa itu `cs` masih selector real-mode.
7. **Apa yang terjadi kalau GDT salah?** Far jump menu ke descriptor invalid → #GP → (bila handler belum ada) double/triple fault → reboot (bootloop).
8. **Apa bedanya VMA dan LMA (final)?** VMA = alamat kode saat berjalan (higher half), LMA = alamat fisik saat GRUB memuat (1 MiB).

---

Bab berikutnya: `03-ch1-framebuffer-interrupt-keyboard.md` — semua baris `portio.c`, `framebuffer.c`, `idt.h/c`, `intsetup.s`, `interrupt.c`, `keyboard.c`, plus cara kerja PIC dan alur satu ketikan keyboard.



---



<!-- ==================== 03-ch1-framebuffer-interrupt-keyboard.md ==================== -->

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



---



<!-- ==================== 04-ch2-disk-ext2.md ==================== -->

# Bab 4 — Chapter 2: Disk Driver dan File System EXT2

Tujuan Chapter 2: OS bisa **menyimpan data secara persisten** ke disk dan mengambilnya kembali, lewat filesystem **EXT2 "IF2130 Edition"**.

File yang dibedah: `disk.h/c` (ATA PIO), `ext2.h` (struktur & konstanta), `ext2.c` (**semua fungsi**, 913 baris), `external-inserter.c` (host tool), dan integrasi Makefile.

---

## 4.1 `disk.h/c` — ATA PIO, blok, LBA

### Konsep
Disk diakses per **blok 512 byte** dengan alamat **LBA** (nomor blok, bukan CHS). ATA PIO memakai port I/O:

| Port | Fungsi |
|---|---|
| `0x1F0` | data (16-bit) |
| `0x1F2` | jumlah blok |
| `0x1F3/4/5` | LBA byte 0/1/2 |
| `0x1F6` | drive + LBA bit 24-27 |
| `0x1F7` | status/command |

Status: `BSY 0x80` (sibuk), `RDY 0x40` (siap), `DRQ 0x08` (data siap), `ERR 0x01`.

### `disk.c` baris demi baris
```c
#define ATA_WAIT_TIMEOUT 1000000u
static void ATA_busy_wait(void) {
    uint32_t timeout = ATA_WAIT_TIMEOUT;
    while ((in(0x1F7) & ATA_STATUS_BSY) && timeout--);
}
static void ATA_DRQ_wait(void) {
    uint32_t timeout = ATA_WAIT_TIMEOUT;
    while (!(in(0x1F7) & ATA_STATUS_RDY) && timeout--);
}
```
- `ATA_busy_wait`: tunggu sampai drive **tidak** sibuk. Ada timeout supaya kernel tidak menggantung bila drive tidak ada (kit asli `while(...);` tanpa batas).
- `ATA_DRQ_wait`: tunggu sampai `RDY` menyala (drive siap menerima/mengirim data).

```c
void read_blocks(void *ptr, uint32_t lba, uint8_t block_count) {
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((lba >> 24) & 0xF)); // 0xE0 = LBA mode + master drive
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) lba);
    out(0x1F4, (uint8_t) (lba >> 8));
    out(0x1F5, (uint8_t) (lba >> 16));
    out(0x1F7, 0x20);                       // command READ SECTORS (dengan retry)
    ...
}
```
1. Tunggu tidak sibuk.
2. `0x1F6`: pilih master, mode LBA, dan LBA bit 24–27 di 4 bit bawah.
3. Kirim LBA 24-bit sisanya (3 port), lalu perintah `0x20`.
```c
    uint16_t *target = (uint16_t*) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)  // 512/2 = 256
            target[j] = in16(0x1F0);                    // baca 2 byte per akses
        target += HALF_BLOCK_SIZE;
    }
```
- Per blok: tunggu BSY/DRQ, lalu baca **256 word** (512 byte) dari port data. Axis pointer digeser 256 word.
- `HALF_BLOCK_SIZE = BLOCK_SIZE/2 = 256`.

`write_blocks` sama, hanya perintah `0x30` dan `out16(0x1F0, ...)`.

**Demo**: "Kenapa 0xE0?" → bit 6–7 = mode LBA, bit 5 = 1, bit 4 = pilih drive master, bit 0–3 = LBA[27:24]. "Kenapa harus baca port walau error?" → Untuk membersihkan status; dan protokol ATA mewajibkan.

---

## 4.2 `ext2.h` — peta filesystem di disk

Konstanta penting (versi final sudah dikoreksi dari kit):
```c
#define BOOT_SECTOR 0
#define DISK_SPACE 4194304u                       // 4 MiB
#define EXT2_SUPER_MAGIC 0xEF53
#define INODE_SIZE sizeof(struct EXT2Inode)       // 64 byte
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // 8 inode per blok
#define GROUPS_COUNT ((BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u)
    // = (512/32)/2 = 8 grup  (kit lupa tanda kurung → nilainya salah; sudah diperbaiki)
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT) // 1024 blok
#define INODES_TABLE_BLOCK_COUNT 16u
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT) // 128
```

Struktur:
- `EXT2Superblock` — metadata global (jumlah inode/blok, magic, jumlah per group).
- `EXT2BlockGroupDescriptor` — per grup: lokasi block bitmap, inode bitmap, inode table, jumlah bebas.
- `EXT2Inode` — `i_mode` (file/folder), `i_size`, `i_blocks`, `i_block[15]`:
  - indeks 0–11: **direct** (menunjuk data langsung),
  - 12: **single indirect** (blok berisi 128 pointer data),
  - 13: **double indirect** (blok pointer ke blok pointer),
  - 14: triple indirect (tidak dipakai).
- `EXT2DirectoryEntry` — `inode` (0 = kosong), `rec_len`, `name_len`, `file_type`, diikuti nama. Direktori = linked list entri di dalam blok data.

Tambahan final: `get_entry_info()` dan `get_file_size()` (deklarasi saja di header).

### Layout disk (hasil `create_ext2`)
```
blok 0      : boot sector (fs_signature)
blok 1      : superblock
blok 2      : tabel BGD
grup 0      : blok 3 = block bitmap, 4 = inode bitmap, 5..20 = inode table
grup 1..7   : mulai dari blok 0 grup → bitmap, bitmap, inode table
data        : sisa blok
```
Grup 0 perlu 3 blok ekstra di depan (superblock+BGD) — itulah `((bgd==0)?3:0)` di `create_ext2`.

---

## 4.3 `ext2.c` — bedah semua fungsi

### A. Signature & cache
```c
const uint8_t fs_signature[BLOCK_SIZE] = { 'C','o','u','r','s','e',' '... , [510]='O',[511]='k' };
```
- 512 byte penanda filesystem, ditulis di blok 0. Dipakai `is_empty_storage()`.
```c
static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;
```
- Superblock & BGD di-cache di RAM agar tidak baca disk tiap operasi.
- `load_superblock`/`store_superblock`: baca/tulis blok 1 lewat `BlockBuffer` (baca 512 byte, memcpy, tulis kembali).
- `load_bgd_table`/`store_bgd_table`: sama untuk blok 2.
- `store_bgd(bgd)`: baca blok 2, tambal **satu** descriptor saja, tulis kembali. Dipakai tiap kali jumlah free berubah agar tidak menulis ulang seluruh tabel.

### B. Bitmap
```c
static bool bitmap_get(const struct BlockBuffer *b, uint32_t i) {
    return (b->buf[i/8] & (1u << (i%8))) != 0;
}
static void bitmap_set(...)   { b->buf[i/8] |= (1u << (i%8)); }
static void bitmap_clear(...) { b->buf[i/8] &= (uint8_t) ~(1u << (i%8)); }
```
- 1 bit per blok/inode. `i/8` = byte ke berapa, `i%8` = bit ke berapa.
- `read_bitmap`/`write_bitmap`: baca/tulis satu blok bitmap.

### C. Pemetaan inode ↔ grup
```c
uint32_t inode_to_bgd(uint32_t inode)   { return (inode-1) / INODES_PER_GROUP; }
uint32_t inode_to_local(uint32_t inode) { return (inode-1) % INODES_PER_GROUP; }
```
- Inode dimulai dari 1; kurangi 1 agar indeks 0-based. Hasilnya: nomor grup dan nomor lokal dalam grup.

### D. Dirent helpers
```c
char *get_entry_name(void *entry)  { return (uint8_t*)entry + sizeof(struct EXT2DirectoryEntry); }
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t off) { return ptr + off; }
struct EXT2DirectoryEntry *get_next_directory_entry(entry) { return (uint8_t*)entry + entry->rec_len; }
uint16_t get_entry_record_len(uint8_t name_len) {
    return (sizeof(entry) + name_len + 3) & ~3u;   // dibulatkan ke kelipatan 4
}
uint32_t get_dir_first_child_offset(void *ptr) {
    entry = get_directory_entry(ptr, 0); off = entry->rec_len;   // lewati "."
    entry = get_directory_entry(ptr, off); off += entry->rec_len; // lewati ".."
    return off;
}
```
- `rec_len` = jarak ke entri berikutnya, harus kelipatan 4 (aturan EXT2) → rumus pembulatan.
- `.` dan `..` selalu dua entri pertama, itulah `first_child_offset`.

### E. Alokasi blok & inode
```c
static uint32_t allocate_block(uint32_t prefered_bgd) {
    for (step = 0; step < GROUPS_COUNT; step++) {
        bgd = (prefered_bgd + step) % GROUPS_COUNT;         // mulai dari grup terdekat
        if (bgd_table.table[bgd].bg_free_blocks_count == 0) continue;
        read_bitmap(...); 
        for (local = 0; local < BLOCKS_PER_GROUP; local++)
            if (!bitmap_get(&bitmap, local)) {              // bit 0 = bebas
                bitmap_set(...); write_bitmap(...);
                bgd->bg_free_blocks_count--; superblock.s_free_blocks_count--;
                store_bgd(bgd);
                return bgd * BLOCKS_PER_GROUP + local;      // nomor blok global
            }
    }
    return 0;                                               // disk penuh
}
```
- Strategi first-fit di grup terdekat; nomor blok global = `grup*BLOCKS_PER_GROUP + lokal`.
- `free_block(block)`: kebalikannya; ambil grup & lokal, kosongkan bit, tambah free, simpan BGD.
- `allocate_node()` sama, tetapi bitmap inode; mengembalikan **inode = lokal + 1** (1-based).
- `free_inode_bit(inode)`: kosongkan bit inode.

### F. Baca/tulis inode
```c
static void read_inode(uint32_t inode, struct EXT2Inode *node) {
    bgd = inode_to_bgd(inode); local = inode_to_local(inode);
    table_block = bgd->bg_inode_table + local / INODES_PER_TABLE;
    offset = (local % INODES_PER_TABLE) * INODE_SIZE;
    read_blocks(&buffer, table_block, 1);
    memcpy(node, buffer.buf + offset, INODE_SIZE);
}
```
- Inode table berukuran 16 blok; `local/8` = blok ke berapa, `local%8` = slot ke berapa; tiap slot 64 byte.
- `sync_node` = kebalikannya (tulis inode kembali ke disk).

### G. Inti alokasi blok data: `node_get_data_block`
```c
static uint32_t node_get_data_block(struct EXT2Inode *node, uint32_t index, bool allocate, uint32_t prefered_bgd)
```
Fungsi tunggal yang menerjemahkan **nomor blok logis file** (`index`) menjadi **nomor blok fisik**:
1. `index < 12` → langsung `node->i_block[index]`. Jika 0 dan `allocate`, alokasikan dan `i_blocks++`.
2. `index -= 12; index < 128` → **single indirect**: pakai `i_block[12]`. Jika belum ada dan `allocate`, alokasikan blok pointer, nolkan, lalu isi pointer data pada indeks tersebut.
3. `index -= 128; index < 128*128` → **double indirect**: `i_block[13]` = blok pointer level-1; `major = index/128` menunjuk pointer level-2, `minor = index%128` menunjuk data. Setiap level dialokasikan dan dinolkan saat pertama dipakai.
4. Sisanya (triple) → 0 (tidak didukung; tidak tercapai di disk 4 MiB).
- Parameter `allocate=false` dipakai untuk **membaca** (jangan mengubah disk).

`allocate_node_blocks(ptr, node, prefered_bgd)`:
```c
block_count = ceil(node->i_size / BLOCK_SIZE);
for i in 0..block_count-1:
    block = node_get_data_block(node, i, true, prefered_bgd);
    chunk = min(sisa, BLOCK_SIZE);
    buffer = nol; memcpy(buffer, ptr + i*512, chunk);   // sisanya nol (padding)
    write_blocks(&buffer, block, 1);
```
- Menyalin isi file dari memory ke blok-blok; blok terakhir di-pad nol.

### H. Baca/tulis potongan (offset-based)
- `dir_read_bytes(node, offset, dst, len)`: baca `len` byte mulai `offset` dengan menghitung `block_index`/`in_block` dan menyalin per potongan. Dipakai untuk membaca dirent (yang bisa melintasi batas blok tanpa melintasi; tapi helper ini tetap aman).
- `dir_write_bytes`: kebalikannya (untuk menghapus entri: menulis 4 byte nol di offset entri).

### I. Scan direktori: `scan_directory`
```c
static void scan_directory(struct EXT2Inode *dir, const char *name, uint8_t name_len,
                           uint8_t file_type, struct ext2_dir_scan *result)
```
- Iterasi semua entri: baca header (`EXT2DirectoryEntry`), lewati `inode==0`, bandingkan `name_len` lalu isi nama dengan `memcmp`.
- `name` NULL/kosong tidak terjadi di sini; untuk "list isi direktori" dipakai `read_directory_data`.
- Hasil: `found` (nama **dan** tipe cocok), `name_matched` (nama cocok tapi tipe beda), `offset`, `entry`.
- `file_type` memisahkan file vs folder dengan nama sama.

### J. Menambah entri: `add_directory_entry`
Tiga langkah:
1. Cari slot **kosong** (`inode==0`) di blok-blok yang sudah ada yang `rec_len`-nya cukup; kalau `rec_len` lebih besar dari kebutuhan, **pecah**: entri baru ambil `needed`, sisanya jadi entri kosong baru (`rest`).
2. Kalau tidak ada slot, coba tambahkan di **ujung blok terakhir** bila sisa ruang cukup (`used_in_last + needed <= 512`), lalu `parent->i_size = used + needed`.
3. Kalau tidak cukup, alokasikan **blok baru** untuk direktori, tulis entri di offset 0, tambah `i_size`.

### K. `init_directory_table`
- Buat blok pertama (`i_block[0]`) untuk direktori baru, tulis dua entri:
  - `.` → inode dirinya, `name_len=1`;
  - `..` → inode parent, `name_len=2`;
- `i_size = rec_len(".") + rec_len("..")`.

### L. Dealokasi
- `free_indirect(block, depth)`: bebas rekursif; `depth` = berapa level pointer.
- `deallocate_blocks(loc, blocks)`: bebaskan 12 direct, lalu single/double indirect sesuai jumlah blok; setiap pointer dikosongkan.
- `deallocate_block(...)`: versi yang memenuhi signature kit; membebaskan array lokasi dan me-refresh bitmap ke `last_bgd`.
- `release_inode_node(node, inode)`: hitung `block_count` dari `i_size`, panggil `deallocate_blocks`, bebaskan bit inode, nolkan & simpan inode.
- `deallocate_node(inode)`: baca inode lalu `release_inode_node`.

### M. Membuat filesystem: `is_empty_storage`, `create_ext2`, `initialize_filesystem_ext2`
```c
bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}
```
- Baca blok 0, bandingkan dengan signature. Tidak sama = kosong.

`create_ext2()`:
1. Tulis signature ke blok 0.
2. Isi superblock: jumlah inode/blok, magic `0xEF53`, jumlah per group.
3. Untuk setiap grup: hitung lokasi bitmap/inode table (+3 blok untuk grup 0), isi 3 descriptor BGD, tandai blok yang terpakai di block bitmap (reserved), tulis kedua bitmap, akumulasi `s_free_blocks_count`.
4. Simpan superblock & BGD.
5. `allocate_node()` → harusnya inode 1; buat root `i_mode = EXT2_S_IFDIR`; `init_directory_table(root, 1, 1)` (parent = dirinya sendiri); `sync_node`.
6. Simpan ulang superblock & BGD.

`initialize_filesystem_ext2()`:
```c
if (is_empty_storage()) create_ext2();
else { load_superblock(); load_bgd_table(); }
```
- Dipanggil `kernel_setup` (dan oleh inserter host). Di OS: bikin FS kalau kosong, kalau tidak cukup muat cache.

### N. CRUD

**`is_directory_empty(inode)`**:
- Baca inode, pastikan direktori; iterasi entri sambil melewati 2 entri pertama (`.` dan `..`); jika ada entri lain dengan `inode != 0` → tidak kosong.

**`read_directory_data(inode, buf, buffer_size)`** (helper):
- Pastikan direktori; `buffer_size` harus ≥ `i_size`; salin semua blok data ke `buf`. Dipakai untuk "taruh isi direktori mentah ke buffer" (shell mem-parse entri).

**`read_directory(request)`**:
- Validasi parent (1..jumlah inode) → error 3.
- Kalau `name == NULL/0`: langsung `read_directory_data(parent_inode, ...)` (mode "baca dir sendiri").
- Kalau ada nama: cari entri bertipe DIR; hasil: 1 = nama ada tapi bukan folder, 2 = tidak ditemukan, atau isi direktori anak.

**`read(request)`**:
- `buf == 0` **sengaja tidak divalidasi** karena program user pertama dimuat ke VA 0.
- Validasi parent; cari entri REG_FILE; error: 1 bukan file, 3 tidak ada, 4 parent invalid.
- Cek `request.buffer_size < node.i_size` → error 2 (buffer kurang).
- Salin blok-blok data ke `request.buf`.

**`get_entry_info(name, name_len, parent, *inode, *type)`**: mirip scan, tapi tanpa filter tipe; dipakai `exec` untuk resolve path.

**`get_file_size(...)`**: cari REG_FILE lalu `*size_out = node.i_size`.

**`write(request)`**:
1. Validasi parent; `is_directory = request->is_directory || buffer_size == 0` (konvensi kit: buffer_size 0 = folder).
2. Cek duplikat dengan `scan_directory` + tipe → error 1 kalau ada.
3. `allocate_node()`; siapkan inode.
   - folder → `init_directory_table`.
   - file → `i_size = buffer_size`; `allocate_node_blocks(buf, node, bgd_inode)`; verifikasi semua blok benar-benar teralokasi (tangani disk penuh dengan rollback).
4. `add_directory_entry(parent, ..., inode)`; jika gagal → rollback inode.
5. `sync_node(node)`, `sync_node(parent)`, simpan superblock & BGD → sukses 0.

**`delete(request)`**:
1. Validasi parent; cari entri sesuai tipe.
2. Jika folder → `is_directory_empty` dulu; error 2 kalau tidak kosong.
3. Kosongkan entri di parent: tulis 4 byte nol di `scan.offset` (`inode=0`) — nama lama diabaikan.
4. `deallocate_node(inode)`; `sync_node(parent)`; simpan cache.

---

## 4.4 `external-inserter.c` — alat host

Jalan di Linux (bukan di OS). Cara kerja:
1. Buka `storage.bin` (4 MiB) ke memory `image_storage`.
2. Implementasi `read_blocks`/`write_blocks` **di atas memory** (bukan port I/O) — itulah kenapa `ext2.c` bisa dipakai ulang.
3. Baca file target, bangun `EXT2DriverRequest` (nama, panjang, parent, buffer, size).
4. Coba `read` dulu; kalau isi sama → cetak `same`.
5. `write`; kalau sudah ada dan opsi `-f`, `delete` lalu `write` lagi.
6. Tulis kembali 4 MiB ke `storage.bin`.

Opsi `-p <nama> <path>` (versi final): buat direktori perantara otomatis (mis. `/bin`) sebelum menulis file.

**Demo**: "Kenapa kode EXT2 yang sama bisa jalan di host dan kernel?" → Karena driver hanya butuh `read_blocks/write_blocks`; kernel memakai port ATA, host memakai file I/O — sisanya (logika filesystem) identik.

---

## 4.5 Integrasi & pengujian

- `kernel_setup` memanggil `initialize_filesystem_ext2()` sebelum proses pertama dimuat.
- `make disk` membuat `bin/storage.bin` 4 MiB; `make inserter` membangun `bin/inserter`; `make insert-shell`/`insert-programs` mengisi disk.
- QEMU: `-drive file=bin/storage.bin,format=raw,if=ide,index=0,media=disk`.
- Uji M2 (saat itu): 18 pemeriksaan CRUD PASS; uji tambahan lewat inserter: file 200 KB (single+double indirect) read-back `same`, `-f` (delete+write) sukses, dan data tetap ada setelah reboot.

---

## 4.6 Tanya-Jawab Demo Ch2

1. **Kenapa blok 512 byte, bukan 4 KiB?** Konvensi sektor ATA; EXT2 IF2130 memakai ukuran blok = sektor.
2. **Apa isi superblock vs BGD?** Superblock = metadata global; BGD = metadata per grup (lokasi bitmap/tabel inode, jumlah bebas).
3. **Bagaimana file >12 blok disimpan?** 12 blok pertama direct; sisanya lewat single indirect (i_block[12]) lalu double indirect (i_block[13]).
4. **Apa fungsi `rec_len`?** Menunjukkan jarak ke entri berikutnya dalam direktori; memungkinkan entri kosong/padding.
5. **Kenapa `read` mengizinkan buf = 0?** Karena program user pertama dimuat ke alamat virtual 0.
6. **Bagaimana tahu disk kosong?** `memcmp` blok 0 dengan `fs_signature`.
7. **Apa bedanya `name_matched` dan `found`?** Nama ada tapi tipenya beda (mis. file vs folder) → kode error berbeda (1 vs 2/3).

---

Bab berikutnya: `05-ch3-paging-usermode-shell.md` — higher-half, paging dua tingkat, recursive mapping, frame allocator, TSS/user mode, syscall, dan shell.



---



<!-- ==================== 05-ch3-paging-usermode-shell.md ==================== -->

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



---



<!-- ==================== 06-ch4-process-scheduler.md ==================== -->

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



---



<!-- ==================== 07-bonus-env-separate-commands.md ==================== -->

# Bab 7 — Bonus: Environment Variables, Perintah Terpisah, dan Ekstra

Bab ini membedah fitur bonus yang diminta Book/Hub:
1. **4.3.5 Environment Variables** — `PATH`, `export`, `env`, persisten di `/path.cfg`.
2. **4.3.6 Separate Shell Commands** — `hello`, `clock`, `echo` sebagai executable terpisah yang dijalankan lewat PATH.
3. Ekstra: `cp`/`rm` folder rekursif, input Shift (agar bisa mengetik `|`, `&`, dll).

Catatan transparansi: dokumen resmi **`IF2130 - Bonus - 2026`** hanya berupa link di Hub dan tidak tersedia di `docs/`, jadi bonus yang dikerjakan adalah yang tertulis eksplisit di Book/Hub. Kalau dokumen bonus resmi diberikan, fitur tambahan bisa disesuaikan.

---

## 7.1 Environment Variables (`PATH`) di `shell.c`

### State
```c
#define PATH_CONFIG_NAME "path.cfg"
static char path_dirs[PATH_DIR_MAX][PATH_MAX];
static uint32_t path_dir_count;
```
- PATH disimpan sebagai daftar direktori (dipisah `:` seperti Unix).
- Default (`path_set_default`): satu entri `/bin`.

### `path_add_entry(index, value, length)` — normalisasi
```c
if (value[0] != '/') { path_dirs[index][0] = '/'; offset = 1; }
for (i < length) path_dirs[index][offset+i] = value[i];
path_dirs[index][offset+length] = '\0';
```
- Memastikan setiap entri PATH **absolut** (diawali `/`), supaya resolusi konsisten dari direktori mana pun.

### `path_load()` — dimuat saat shell mulai
```c
path_set_default();                                        // minimal /bin
if (query_file_size("path.cfg", 1, &size) != 0 || ...) return;
read_file("path.cfg", 1, file_buffer, ...);
file_buffer[size] = '\0';
// pecah berdasarkan ':' → path_add_entry untuk setiap segmen
if (path_dir_count == 0) path_set_default();
```
- Membaca `/path.cfg` (root inode 1) dan mengurai `:`; jika file tidak ada/gagal, tetap pakai default.
- Inilah yang membuat PATH **bertahan setelah reboot**.

### `path_save()` — ditulis saat `export`
```c
susun string "dir1:dir2";
if (path.cfg sudah ada) delete_entry("path.cfg", 1, false);
write_file("path.cfg", 1, config, strlen(config));
```
- Karena EXT2 IF2130 tidak punya "truncate/overwrite", file lama dihapus lebih dulu baru ditulis ulang.

### `path_set(value)` — dipanggil `export`
```c
// pecah value berdasarkan ':' → count entri
if (count == 0) { print_error("export: invalid PATH", NULL); return; }
path_dir_count = count;
path_save();     // persisten
print_path();    // cetak "PATH=..."
```

### `print_path()` dan perintahnya
- `env` → memanggil `print_path()` (cetak `PATH=...`).
- `export PATH /bin` → `path_set("/bin")`. (Shell menerima juga `export /bin` persis seperti yang diuji.)

### `resolve_executable(name, out, cap)` — inti bonus 4.3.6
```c
if (name diawali '/' atau mengandung '/') {
    make_absolute(name, candidate);           // path eksplisit
    if (file_exists(candidate)) { copy; return true; }
    return false;
}
make_absolute(name, candidate);
if (file_exists(candidate)) { ... }           // relatif terhadap cwd

for (i = 0; i < path_dir_count; i++) {        // telusuri PATH
    candidate = path_dirs[i] + "/" + name;
    if (file_exists(candidate)) { copy; return true; }
}
return false;
```
- Urutan: (1) path absolut/berisi `/`, (2) relatif cwd, (3) tiap direktori PATH.
- `file_exists()` memakai syscall `read_directory` untuk mencari entri.

---

## 7.2 Separate Shell Commands

### Program eksternal
| Program | Isi | Cara pakai |
|---|---|---|
| `/bin/hello` | cetak "Hello World! ..." lalu `return` (exit) | `hello` |
| `/bin/echo` | cetak argumen `argv[1..]` | `echo halo dunia` |
| `/bin/clock` | loop baca RTC (syscall 14) + `write_at` (18) ke pojok kanan bawah | `clock` (background) |

Ketiganya masuk kategori **perintah terpisah**: kode dan address space-nya sendiri, dijalankan lewat mekanisme `exec` + PATH, bukan builtin shell.

### Alur eksekusi `hello`
```
run_command("hello") → is_builtin? tidak
→ resolve_executable("hello") → "/bin/hello"
→ syscall EXEC (11): kernel resolve "/bin/hello" → parent=/bin, nama="hello"
   → process_create_user_process_args() → PCB READY (mis. PID 2)
→ kernel return PID; shell cetak "[2] started hello"
→ IRQ0 berikutnya → scheduler memilih PID 2 → context switch → ring 3
→ _start → main() → cetak → syscall EXIT (10) → process_destroy
→ scheduler kembali ke shell
```
- Perhatikan: `crt0.s` otomatis memanggil `exit` setelah `main`, jadi tidak ada proses zombie yang menggantung.

### Builtin vs eksternal
```c
static bool is_builtin(cmd) {
    static const char *builtins[] = { "help","clear","pwd","cd","ls","mkdir","cat",
        "cp","rm","mv","find","grep","exec","ps","kill","env","export" };
}
```
- Perintah yang butuh akses langsung ke state shell (cwd, buffer, PATH) tetap **builtin**.
- Perintah yang bisa berdiri sendiri (`hello`, `clock`, `echo`) dijadikan **eksternal** lewat PATH.
- `run_command` juga mendeteksi pipeline `|` (memakai `io_buffer` dua lapis: `pipe_out` → `pipe_in`).

### `exec` dari shell (`cmd_exec`)
```c
background = (argterakhir == "&");
resolve_executable(argv[1], absolute);
pid = syscall(SYS_EXEC, absolute, &argv[1], background);
if (pid < 0) error; else print "[pid] started <nama>";
```
- `argv[1]` adalah pointer ke `"nama\0arg1\0arg2..."`; kernel mengumpulkan 8 argumen maksimum.
- `&` hanya penanda; `exec` di OS ini memang tidak menunggu (background semantics).

---

## 7.3 Recursive `cp` / `rm` (folder)

### `collect_children`
```c
while (offset + sizeof(entry) <= DIR_BUFFER_SIZE && count < 48) {
    entry = entry_at(offset);
    if (entry->inode != 0 && !is_dot_entry(entry) && entry->name_len < 64) {
        children[count] = {inode, file_type, name_len, name};
        count++;
    }
    offset += entry->rec_len;
}
```
- Mengumpulkan semua anak direktori (selain `.`/`..`) dari `dir_buffer` (isi direktori yang sudah dimuat `load_directory`).

### `copy_tree(src_inode, dst_parent, dst_name, depth)`
1. Batas kedalaman 6 (mencegah rekursi tak terbatas).
2. `load_directory(src)` → `collect_children`.
3. `make_directory(dst_name, dst_parent)` → `find_in_dir` untuk mendapat inode tujuan.
4. Untuk setiap anak:
   - folder → rekursi `copy_tree(..., depth+1)`;
   - file → `query_file_size` + `read_file` + `write_file` (isi disalin via `file_buffer`).

### `remove_tree(inode, parent_inode, name, file_type)`
1. Bukan folder → `delete_entry` biasa.
2. Folder → `collect_children`, **rekursi menghapus semua anak dulu**, baru `delete_entry(name, parent, true)`.
- Urutan ini penting: EXT2 `delete` menolak folder yang tidak kosong.

### Dipakai oleh
- `cmd_cp` → `copy_tree`.
- `cmd_rm` → `remove_tree`.
- `cmd_mv` → `copy_tree` + `remove_tree` (rename silang direktori).

---

## 7.4 Shift keyboard

`keyboard.c` final menambahkan:
```c
const char keyboard_scancode_1_to_ascii_shift_map[256] = {
      0,   0, '!', '@', '#', '$', '%', '^',  '&', '*', ...
    'Q','W','E', ...
};
#define SCANCODE_LEFT_SHIFT  0x2A
#define SCANCODE_RIGHT_SHIFT 0x36
#define SCANCODE_LEFT_SHIFT_RELEASE  0xAA
#define SCANCODE_RIGHT_SHIFT_RELEASE 0xB6
static bool keyboard_shift_pressed = false;
```
- Saat make Shift (`0x2A/0x36`) → `shift_pressed = true`; saat break (`0xAA/0xB6`) → false.
- Karakter diambil dari `shift_map` bila Shift ditahan, selain itu `map`.
- **Kenapa penting?** Tanpa ini, pipeline (`|`), background (`&`), dan tanda `/` tidak bisa diketik dengan benar (QEMU mengirim scancode sama, beda shift).

---

## 7.5 Cara Mendemonstrasikan Bonus (urutan praktis)

```bash
make prepare && make run
```
Di QEMU:
```
env                      → PATH=/bin
export PATH /bin         → PATH=/bin          (tersimpan ke /path.cfg)
hello                    → Hello World! ...   (eksternal, PID diberikan)
echo hello dunia         → hello dunia        (eksternal)
clock                    → [HH:MM:SS] di pojok kanan bawah, prompt tetap hidup
ps                       → PID 1 shell RUNNING; PID x clock READY; total N process(es)
kill <pid clock>         → killed pid x; ps kembali bersih
mkdir noirtest && ls     → folder baru muncul
cp -r? (folder)          → cp folder1 folder2 → rekursif
rm folder2               → terhapus rekursif
# reboot (keluar QEMU lalu jalankan lagi):
env                      → PATH=/bin tetap ada (persistensi)
```
Bukti yang sudah diambil saat pengerjaan: screenshot `clock &` + `ps`, `exec hello`, `env` sebelum/sesudah reboot, dan `kill 2` (clock hilang dari `ps`).

---

## 7.6 Tanya-Jawab Demo Bonus

1. **Bagaimana PATH bertahan setelah reboot?** `path_save()` menulis `/path.cfg` ke EXT2; `path_load()` membacanya saat shell mulai.
2. **Kenapa PATH harus absolut?** Agar resolusi tidak bergantung pada cwd; `path_add_entry` otomatis menambahkan `/`.
3. **Apa bedanya builtin dan perintah terpisah?** Builtin berbagi address space & state shell; perintah terpisah punya PCB/address space sendiri dan dijalankan lewat `exec`.
4. **Kenapa `cp` folder harus rekursif?** EXT2 menyimpan direktori sebagai tree; menyalin satu level saja akan kehilangan isi anak.
5. **Kenapa `rm` folder harus menghapus anak dulu?** `delete` menolak direktori tidak kosong (konsistensi filesystem).
6. **Kenapa butuh Shift map?** Shell butuh karakter `|`/`&`/`/` yang hanya ada di posisi shift pada keyboard US.
7. **Apa yang belum dikerjakan (jujur di demo)?** Bonus resmi belum dicek (dokumen tidak tersedia); perintah FS inti masih builtin; `grep` literal belum regex; belum ada swap/page replacement.

---

Bab terakhir: `08-demo-qna.md` — kumpulan tanya-jawab demo/ujian dari semua bab + daftar "hafalan minimal".



---



<!-- ==================== 08-demo-qna.md ==================== -->

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



---
