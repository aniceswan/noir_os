![Credit: 朧月](mascot.jpg)

# mbOSok - Tugas Besar IF2130 Sistem Operasi 2026/2027

Template dasar untuk Tugas Besar IF2130 - Sistem Operasi 2026/2027

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
4. [Maskot Kelompok](#maskot-kelompok)

## Cara Run

Prasyarat (Linux): `nasm`, `gcc` + `gcc-multilib`, `make`, `genisoimage`, `qemu-system-x86`, `gdb`.

```bash
sudo apt update
sudo apt install -y nasm gcc gcc-multilib make genisoimage qemu-system-x86 gdb
```

Build dan jalankan:

```bash
make build   # menghasilkan bin/OS2025.iso
make run     # menjalankan OS lewat QEMU (gdb server di localhost:1234)
```

Untuk debug di VS Code, gunakan konfigurasi `Kernel` pada `.vscode/launch.json`.

## Fitur yang Dibuat

- [x] Chapter 0: Toolchain, C kernel + linker, Makefile automation, GDT
- [x] Chapter 1: Framebuffer driver, Interrupt (PIC remap, IDT, ISR), Keyboard driver
- [x] Chapter 2: File System EXT2
- [ ] Chapter 3: Paging, User Mode, Shell
- [ ] Chapter 4: Process, Scheduler, Multitasking
- [ ] Bonus / Side Features

## Maskot Kelompok

![Maskot Kelompok](mascot.jpg)

Credit: 朧月
