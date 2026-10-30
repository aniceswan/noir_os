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
