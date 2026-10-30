![Credit: 朧月](mascot.jpg)

# mbOSok - Tugas Besar IF2130 Sistem Operasi 2026/2027

Sistem operasi 32-bit x86 (C + NASM, freestanding) hasil pengerjaan Tugas Besar
IF2130. Kernel berjalan di higher-half (`0xC0100000`), mendukung paging,
user mode, system call, EXT2, process, scheduler, dan multitasking preemptive.

## Nama Kelompok

- **Nama Kelompok:** mbOSok
- **Anggota:**
  - Nabil Rabbani — 13525004
  - Raditya Wibian Sastaka — 13525019
  - Mochammad Nuha Al Ghifari — 13525056
  - Ghaniyul Amri Caulava — 13525106

## Daftar Isi

1. [Nama Kelompok](#nama-kelompok)
2. [Cara Run](#cara-run)
3. [Fitur yang Dibuat](#fitur-yang-dibuat)
4. [Arsitektur Singkat](#arsitektur-singkat)
5. [Maskot Kelompok](#maskot-kelompok)

## Cara Run

Prasyarat (Linux): `nasm`, `gcc` + `gcc-multilib`, `make`, `genisoimage`,
`qemu-system-x86`, `gdb`, `qemu-img` dan `python3` + `PIL` (opsional, untuk uji).

```bash
sudo apt update
sudo apt install -y nasm gcc gcc-multilib make genisoimage qemu-system-x86 gdb
```

Build lengkap (kernel + disk EXT2 + shell + seluruh program user):

```bash
make prepare   # clean + build ISO + buat disk 4 MiB + insert semua program
make run       # jalankan lewat QEMU (gdb server di localhost:1234)
```

Perintah build yang lebih granular:

```bash
make build             # bin/OS2025.iso
make disk              # bin/storage.bin (4 MiB, menghapus data lama)
make user-programs     # shell, clock, hello, echo (+ .elf untuk debug)
make inserter          # host tool bin/inserter
make insert-shell      # masukkan shell ke root disk
make insert-programs   # masukkan clock/hello/echo ke /bin lewat PATH
```

Untuk debug di VS Code, gunakan konfigurasi `Kernel` pada `.vscode/launch.json`
(symbol kernel + `shell_elf` sudah dimuat otomatis).

## Fitur yang Dibuat

- [x] Chapter 0: Toolchain, C kernel + linker, Makefile automation, GDT
- [x] Chapter 1: Framebuffer driver, Interrupt (PIC remap, IDT, ISR), Keyboard driver
- [x] Chapter 2: File System EXT2 (CRUD file & folder, indirect block, host inserter)
- [x] Chapter 3: Paging (higher-half, recursive page directory, frame allocator),
      User Mode (TSS, ring 3), System Call `int 0x30`, Shell (ls/cat/mkdir/cd/cp/rm/
      mv/find/grep/echo + pipeline)
- [x] Chapter 4: Process, Scheduler, Multitasking
  - [x] Multi virtual address space (page directory per proses)
  - [x] Process Control Block (Context, state, memory & metadata)
  - [x] `process_create_user_process`, `process_destroy`, PID, init process = shell
  - [x] PIT IRQ0 1000 Hz, Round-Robin preemptive scheduler
  - [x] Context switch assembly (`process_context_switch`) + koreksi intsetup/CPURegister
  - [x] Process entrypoint & exit (`crt0.s` + syscall `exit`)
  - [x] Process management: syscall `exec`, `kill`, `ps` + command shell
  - [x] CMOS/RTC driver + program `clock` (update tiap detik di pojok kanan bawah)
  - [x] External application `hello` dijalankan lewat `exec`
  - [x] Background process (`clock &`, `exec`), shell tetap responsif
- [x] Bonus 4.3.5 Environment Variables: `PATH` + `export`, persistent di `/path.cfg`
- [x] Bonus 4.3.6 Separate Shell Commands: `echo` (dan seluruh program di `/bin`)
      dijalankan dengan nama saja melalui PATH, lengkap dengan argc/argv
- [x] Bonus folder `cp` / `rm` rekursif (Chapter 3)
- [x] Debug symbols untuk shell & program user (`bin/*_elf`)

## Arsitektur Singkat

- **Memori**: kernel di higher-half (`0xC0000000+`), satu PDE[0x300] dipakai
  bersama seluruh page directory; PDE[1023] bersifat rekursif (fractal).
- **Proses**: tiap proses mendapat page directory sendiri, image executable
  dimuat ke virtual address `0x0`, stack user di `0x3FC000-0x3FFFFF`.
- **Scheduling**: timer PIT 1000 Hz memicu `main_interrupt_handler` IRQ0 yang
  menyimpan context ke PCB dan memanggil `scheduler_switch_to_next_process`
  (round robin). Kernel memakai satu kernel stack (TSS `esp0`) tanpa reentrancy.
- **Syscall**: `int 0x30` DPL 3, return value di `eax` (lihat
  `src/header/user/syscall.h`).

## Maskot Kelompok

![Maskot Kelompok](mascot.jpg)

Credit: 朧月
