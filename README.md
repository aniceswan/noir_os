![Credit: 朧月](mascot.jpg)

# mbOSok

Sistem operasi 32-bit x86 Protected Mode buatan kelompok **mbOSok** untuk Tugas Besar IF2130 Sistem Operasi 2026/2027. mbOSok punya kernel higher-half dengan paging, file system EXT2, shell di user mode, dan multitasking preemptive: beberapa program bisa berjalan bersamaan, misalnya jam yang terus berdetak di pojok layar selagi shell tetap dipakai.

## Daftar Isi

1. [Kontributor](#kontributor)
2. [Cara Run](#cara-run)
3. [Cara Pakai](#cara-pakai)
4. [Fitur yang Dibuat](#fitur-yang-dibuat)
5. [Struktur Kode](#struktur-kode)
6. [Maskot Kelompok](#maskot-kelompok)

## Kontributor

| Nama | NIM |
|------|-----|
| Nabil Rabbani | 13525004 |
| Raditya Wibian Sastaka | 13525019 |
| Mochammad Nuha Al Ghifari | 13525056 |
| Ghaniyul Amri Caulava | 13525106 |

## Cara Run

Prasyarat (Linux / WSL): `nasm`, `gcc` + `gcc-multilib`, `make`, `genisoimage`, `qemu-system-x86`, `gdb`.

```bash
sudo apt update
sudo apt install -y nasm gcc gcc-multilib make genisoimage qemu-system-x86 gdb
```

Build dan jalankan:

```bash
make prepare   # build kernel (bin/OS2025.iso), buat disk baru, lalu masukkan shell, clock, hello, dan echo
make run       # menjalankan OS lewat QEMU (gdb server di localhost:1234)
```

`make prepare` membuat ulang `bin/storage.bin`, jadi isi disk sebelumnya hilang. Untuk build ulang kernel tanpa menyentuh disk, pakai `make build`.

`make run` menjalankan QEMU dengan flag `-S`, sehingga QEMU menunggu debugger sebelum boot. Lanjutkan dengan konfigurasi `Kernel` pada `.vscode/launch.json`, atau dari terminal lain:

```bash
gdb -ex "target remote :1234" -ex "continue"
```

## Cara Pakai

Setelah boot, shell langsung siap menerima command. Ketik `help` untuk daftar lengkapnya.

| Command | Fungsi |
|---------|--------|
| `ls`, `cd`, `pwd`, `mkdir` | navigasi dan membuat folder |
| `cat`, `cp`, `mv`, `rm` | membaca, menyalin, memindahkan, dan menghapus file atau folder |
| `find <nama>`, `grep <pola> [file]` | pencarian, `grep` juga bisa dipakai di pipeline (`ls \| grep txt`) |
| `exec <file> [argumen] [&]` | menjalankan program, `&` menjalankannya di background |
| `ps` | menampilkan PID, parent PID, state, dan nama semua process |
| `kill <pid>` | menghentikan process berdasarkan PID |
| `env`, `export <dir[:dir...]>` | melihat dan mengubah variabel `PATH` |
| `clear`, `help` | membersihkan layar dan menampilkan bantuan |

Program di dalam `PATH` (bawaan: `/bin`) bisa dijalankan langsung dengan namanya dari folder mana pun.

```text
mbOSok/$ hello
Hello World!
mbOSok/$ clock &
[3] clock started in background
mbOSok/$ ps
PID   PPID  STATE    NAME
1     0     RUNNING  shell
3     1     READY    clock
total 2 process(es)
mbOSok/$ kill 3
killed pid 3
```

- `clock` menampilkan jam dari CMOS di pojok kanan bawah layar. Jalankan dengan `&` supaya shell tetap bisa dipakai.
- Program tanpa `&` berjalan di foreground: shell menunggu sampai program selesai. Tekan `Ctrl+C` untuk menghentikannya.
- Program yang crash (page fault, general protection fault, dan sejenisnya) hanya menghentikan dirinya sendiri, process lain tetap berjalan.

## Fitur yang Dibuat

- [x] Chapter 0: Toolchain, C kernel + linker, Makefile automation, GDT
- [x] Chapter 1: Framebuffer driver, Interrupt (PIC remap, IDT, ISR), Keyboard driver (Shift, Caps Lock, Ctrl+C)
- [x] Chapter 2: File System EXT2
- [x] Chapter 3: Paging, User Mode, Shell
- [x] Chapter 4: Process, Scheduler, Multitasking
  - Process Control Block dan virtual address space terpisah untuk setiap process
  - Scheduler preemptive Round Robin dengan timer interrupt (IRQ0) dan context switch
  - Syscall `exit`, `exec`, `kill`, `ps`, serta command shell `exec`, `ps`, `kill`
  - Driver CMOS dan program `clock` yang berjalan di background
  - Aplikasi eksternal `hello` yang dijalankan lewat `exec`
- [x] Bonus: Environment variable `PATH` yang persisten (disimpan di `path.cfg`)
- [x] Bonus: Separate shell command (`echo` sebagai binary eksternal di `/bin/echo`)

## Struktur Kode

| Lokasi | Isi |
|--------|-----|
| `src/kernel.c`, `src/kernel-entrypoint.s` | entrypoint kernel dan urutan inisialisasi |
| `src/cpu/` | GDT, IDT, interrupt handler, dan dispatcher syscall |
| `src/memory/` | paging dan page frame allocator |
| `src/process/` | process, scheduler, dan context switch |
| `src/filesystem/` | driver disk ATA dan EXT2 |
| `src/driver/`, `src/text/` | keyboard, CMOS, dan framebuffer |
| `src/user/` | program user mode: `shell`, `clock`, `hello`, `echo`, beserta `crt0.s` |
| `src/external/` | inserter, alat host untuk mengisi disk image |

## Maskot Kelompok

![Maskot Kelompok](mascot.jpg)

Credit: 朧月
