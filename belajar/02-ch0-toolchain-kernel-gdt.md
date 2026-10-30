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
