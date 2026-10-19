# Jurnal Pengerjaan Tugas Besar IF2130 - Sistem Operasi
## Milestone 2: File System EXT2 (IF2130 Edition)

---

### Informasi Kelompok
- **Nama Kelompok:** mbOSok
- **Anggota Kelompok:**
  1. Nabil Rabbani — 13525004
  2. Raditya Wibian Sastaka — 13525019
  3. Mochammad Nuha Al Ghifari — 13525056
  4. Ghaniyul Amri Caulava — 13525106
- **Tautan Repositori:** [https://github.com/ghaniyul/mbOSok](https://github.com/ghaniyul/mbOSok)
- **Branch:** `main`
- **Target Milestone:** Milestone 2 (v0.2.x) — File System EXT2

---

## 1. Ringkasan Implementasi Bab / Fitur

Pada Milestone 2 ini, kelompok telah menyelesaikan seluruh spesifikasi **Chapter 2 File System: EXT2 - IF2130 Edition** yang mencakup subsistem penyimpanan persisten non-volatile dari level hardware I/O bus hingga level abstraksi file dan folder:

1. **Port I/O 16-Bit (`in16`, `out16`)**:
   - Implementasi wrapper instruksi assembly `inw` dan `outw` pada `src/cpu/portio.c` dan `src/header/cpu/portio.h` untuk membaca dan menulis data 2-byte sekaligus melalui I/O bus x86.
2. **Driver Disk ATA PIO 28-bit LBA**:
   - Implementasi driver `src/driver/disk.c` dan `src/header/driver/disk.h` untuk komunikasi dengan disk controller IDE/ATA pada primary bus (port `0x1F0` - `0x1F7`).
   - Penanganan status `ATA_STATUS_BSY` (`0x80`) dan `ATA_STATUS_RDY` / DRQ (`0x08`) via busy-waiting loop.
   - Operasi baca (`read_blocks`) dan tulis (`write_blocks`) per satuan blok 512-byte (256 * 16-bit word).
3. **Ekspansi Kernel Stack**:
   - Peningkatan ukuran stack kernel dari 4 KiB menjadi 2 MiB (`KERNEL_STACK_SIZE equ 2097152`) pada `src/kernel-entrypoint.s` guna mencegah terjadinya stack overflow saat alokasi buffer blok filesystem dan pemanggilan fungsi mendalam.
4. **Driver EXT2 - IF2130 Edition Core**:
   - Layout sistem berkas: 4 MiB disk (`storage.bin`), ukuran blok 512 byte (total 8192 blok), dibagi menjadi 8 Block Groups (masing-masing 1024 blok).
   - Inode berukuran 128 byte (4 inode per blok, 16 blok tabel inode per group = 64 inode per group, total 512 inode).
   - Penanganan Superblock (blok 1), Block Group Descriptor Table (blok 2), Block Bitmap (1 blok/group), Inode Bitmap (1 blok/group), dan Inode Table (16 blok/group).
5. **Operasi CRUD File System**:
   - `read`: Membaca file reguler berdasarkan path parent dan nama file, mendukung pembacaan bertingkat direct block hingga indirect block.
   - `read_directory`: Membaca entri direktori dan memuat seluruh `EXT2DirectoryEntry` ke dalam buffer pemanggil.
   - `write`: Menulis file atau membuat direktori baru dengan algoritma alokasi First-Fit, otomatis mengalokasikan direct, single, double, dan triple indirect pointer sesuai kebutuhan.
   - `delete`: Menghapus file atau direktori kosong, melakukan dealokasi blok data, blok penunjuk indirection, serta membebaskan bit pada Block Bitmap dan Inode Bitmap.
6. **Inisialisasi & Verifikasi Otomatis**:
   - `is_empty_storage`: Memverifikasi signature boot sector `fs_signature`.
   - `create_ext2`: Menginisialisasi disk kosong dengan menulis signature, superblock, tabel BGD, bitmap kosong, dan root directory (inode 2).
   - `initialize_filesystem_ext2`: Melakukan auto-format jika disk kosong, atau memuat metadata dari disk jika sudah ada.

---

## 2. Masalah yang Dihadapi (Problems Encountered)

### Masalah 1: Batasan Ukuran Kernel Stack Awal (4 KiB)
- **Deskripsi Masalah:** Pada konfigurasi awal kit, kernel stack hanya dialokasikan sebesar 4096 byte (4 KiB). Saat fungsi-fungsi filesystem dijalankan—yang memerlukan struktur lokal seperti `struct BlockBuffer` (512 byte), `struct EXT2Inode` (128 byte), tabel direktori, serta variabel sementara lainnya—terjadi penumpukan call stack yang cepat. Hal ini memicu terjadinya *Silent Stack Overflow*, menimpa memori data kernel lain dan berujung pada CPU Triple Fault / bootloop QEMU.
- **Gejala:** QEMU langsung mereset sistem secara berulang (bootloop) sesaat setelah fungsi penulisan atau pembacaan blok filesystem dipanggil.

### Masalah 2: Handshake & Polling Status pada ATA PIO Controller
- **Deskripsi Masalah:** Komunikasi data dengan ATA controller port `0x1F0` sangat bergantung pada timing controller hardware. Jika pembacaan atau penulisan data dilakukan sebelum bit `DRQ` (Data Request) aktif atau saat bit `BSY` (Busy) masih bernilai 1, data yang dibaca dari port `0x1F0` menjadi korup (`0xFFFF` atau sampah), dan penulisan blok tidak tersimpan di storage virtual.
- **Gejala:** Operasi `read_blocks` mengembalikan byte acak dan signature disk gagal terdeteksi secara konsisten.

### Masalah 3: Navigasi Indirection Inode pada Ukuran Blok Kecil (512 Byte)
- **Deskripsi Masalah:** Pada EXT2 standar Linux, ukuran blok umumnya 1 KiB atau 4 KiB. Pada IF2130 Edition, ukuran blok adalah 512 byte, sehingga satu blok penunjuk indirection hanya dapat menampung `512 / 4 = 128` buah pointer 32-bit. Menghitung offset secara presisi untuk direct (0–11), single indirect (12–139), double indirect (140–16523), dan triple indirect membutuhkan kehati-hatian matematis agar tidak terjadi *off-by-one error* pada index blok.
- **Gejala:** Data file yang melebihi 6 KiB (12 blok pertama) terpotong atau tertulis ke blok yang salah pada pengujian file besar.

### Masalah 4: Batasan Header C dan Reusabilitas Kode ke Host Program (`inserter`)
- **Deskripsi Masalah:** Spesifikasi Buku Panduan (Halaman 87 dan 130–133) mewajibkan file `src/filesystem/ext2.c` **hanya** mengimpor `disk.h`, `ext2.h`, `stdint.h`, `stdbool.h`, dan `string.h`. Hal ini karena kode driver `ext2.c` akan dikompilasi ulang oleh GCC di host OS Linux/WSL untuk membuat binary utilitas `inserter` pada Bab 3 (`external-inserter.c`). Apabila `ext2.c` mengandung header internal kernel (seperti `portio.h`, `framebuffer.h`, atau assembly wrapper), proses build `inserter` pada host OS akan gagal kompilasi akibat *unresolved symbols*.
- **Gejala:** Ketergantungan yang tidak modular membuat kode tidak dapat di-link bersama implementasi driver disk tiruan milik host OS.

### Masalah 5: Integritas Struktur Direktori (Entri `.` dan `..`)
- **Deskripsi Masalah:** Sesuai batasan spesifikasi 2.4.5, direktori baru harus selalu diawali oleh tepat 2 entri: `.` (menunjuk dirinya sendiri) dan `..` (menunjuk parent). Namun pada direktori root, parent dari dirinya adalah root itu sendiri. Jika proses penghapusan direktori (`delete`) tidak memvalidasi keberadaan entri selain `.` dan `..`, direktori yang masih memiliki anak bisa terhapus, menyebabkan *orphan inodes* dan kerusakan struktur pohon direktori.

---

## 3. Analisis dan Solusi Penyelesaian (Analysis & Solution)

### Solusi Masalah 1: Ekspansi Kernel Stack ke 2 MiB
- **Analisis:** Penanganan I/O dan traversal filesystem membutuhkan ruang call frame yang memadai. Sesuai panduan bab 2.6 (halaman 88), ukuran stack dinaikkan menjadi `2097152` byte (2 MiB).
- **Penyelesaian:** Diubah konstanta `KERNEL_STACK_SIZE equ 2097152` pada `src/kernel-entrypoint.s`. Stack kini memiliki headroom yang sangat lapang untuk menampung buffer blok, rekursi fungsi, dan context saving di milestone mendatang.

### Solusi Masalah 2: Dua Tahap Polling Sinkron pada ATA Driver
- **Analisis:** Berdasarkan spesifikasi ATA/ATAPI, sebelum mengirim perintah harus dipastikan controller tidak sibuk (`BSY = 0`). Setelah perintah `0x20` (Read) atau `0x30` (Write) dikirim, driver harus menunggu hingga drive siap mentransfer blok data (`DRQ = 1` dan `BSY = 0`).
- **Penyelesaian:** Diimplementasikan fungsi `ATA_busy_wait()` yang melakukan polling pada bit 7 (BSY) dan `ATA_DRQ_wait()` pada bit 3 (DRQ) port `0x1F7` sebelum dan sesudah transfer 256 word via instruksi `in16(0x1F0)` / `out16(0x1F0)`.

### Solusi Masalah 3: Abstraksi Modular Pembacaan & Penulisan Blok Inode
- **Analisis:** Mengakses blok data berdasarkan indeks logis (0 hingga $N-1$) dapat diabstraksikan melalui fungsi pembantu terpadu `ext2_get_or_alloc_file_block()` dan `ext2_free_inode_blocks()`.
- **Penyelesaian:** 
  - Blok 0 s.d. 11 dipetakan langsung ke array `i_block[0..11]`.
  - Blok 12 s.d. 139 dipetakan melalui Single Indirect Block pada `i_block[12]`.
  - Blok 140 ke atas dipetakan melalui Doubly Indirect Block pada `i_block[13]`.
  - Fungsi membaca dan menulis blok indeks dengan First-Fit alokasi jika blok belum ada, serta melakukan sinkronisasi blok penunjuk ke storage secara instan.

### Solusi Masalah 4: Isolasi Arsitektur Bersih pada `ext2.c`
- **Analisis:** Menjadikan `ext2.c` murni bergantung pada interface `read_blocks` dan `write_blocks` yang dideklarasikan di `header/driver/disk.h`, tanpa memanggil fungsi I/O CPU atau fungsi kernel lain.
- **Penyelesaian:** 
  - Header yang disertakan pada `src/filesystem/ext2.c` dibatasi ketat hanya pada:
    `#include "header/driver/disk.h"`
    `#include "header/filesystem/ext2.h"`
    `#include <stdint.h>`
    `#include <stdbool.h>`
    `#include "header/stdlib/string.h"`
  - Seluruh operasi string menggunakan implementasi freestanding `memcpy`, `memset`, `memcmp`, `strlen`, `strcpy` milik kernel di `src/stdlib/string.c`.
  - Terbukti dapat langsung di-link bersama `external-inserter.c` di host OS tanpa modifikasi apapun.

### Solusi Masalah 5: Validasi Direktori Kosong & Proteksi Root
- **Analisis:** Sebelum menghapus inode bertipe direktori, isi tabel direktori harus diperiksa.
- **Penyelesaian:**
  - Ditambahkan fungsi validasi yang memindai seluruh entri pada blok direktori target. Jika ditemukan entri selain `.` dan `..`, fungsi `delete()` mengembalikan kode error `2` (folder not empty).
  - Ditambahkan pemeriksaan `request.parent_inode == 2 && strcmp(request.name, "..") == 0` untuk mencegah penghapusan root directory.

---

## 4. Hasil Pengujian dan Verifikasi (Testing & Verification)

Pengujian dilakukan secara komprehensif menggunakan self-test terotomatisasi di kernel (`src/kernel.c`) dan verifikasi biner disk `storage.bin`:

| No | Kasus Uji | Skenario Pengujian | Hasil yang Diharapkan | Status |
|:---|:---|:---|:---|:---:|
| 1 | Inisialisasi Storage | Disk kosong pertama kali di-boot | `is_empty_storage() == true`, `create_ext2()` berhasil membuat struktur EXT2 | **PASS** |
| 2 | Write File di Root | Tulis file baru `test.txt` ukuran 100 byte | Return code 0, entri muncul di tabel direktori root | **PASS** |
| 3 | Read File | Baca kembali file `test.txt` | Return code 0, isi buffer identik dengan yang ditulis | **PASS** |
| 4 | Write Duplicate | Tulis kembali file `test.txt` di folder yang sama | Return code 1 (Entry already exists) | **PASS** |
| 5 | Read Buffer Insufficient | Baca file dengan `buffer_size` lebih kecil dari ukuran file | Return code 2 (Buffer tidak cukup) | **PASS** |
| 6 | Read Non-existent File | Baca file yang belum pernah dibuat | Return code 3 (File tidak ditemukan) | **PASS** |
| 7 | Read Invalid Parent | Baca file dengan parent inode yang bukan direktori | Return code 4 (Parent folder invalid) | **PASS** |
| 8 | Read Directory | Baca daftar isi folder root | Return code 0, memuat entri `.`, `..`, dan file-file di dalamnya | **PASS** |
| 9 | Make Directory | Buat folder baru `docs/` | Return code 0, terinisialisasi entri `.` dan `..` | **PASS** |
| 10 | Nested File Write | Tulis file `readme.md` di dalam folder `docs/` | Return code 0, teralokasi dengan parent inode `docs/` | **PASS** |
| 11 | Delete Non-empty Folder | Hapus folder `docs/` saat masih berisi file | Return code 2 (Folder yang akan dihapus tidak kosong) | **PASS** |
| 12 | Delete Nested File | Hapus file `docs/readme.md` | Return code 0, blok data dan inode dibebaskan | **PASS** |
| 13 | Delete Empty Folder | Hapus folder `docs/` setelah kosong | Return code 0, folder berhasil dihapus dari parent | **PASS** |
| 14 | Delete Non-existent | Hapus file/folder yang tidak ada | Return code 1 (Entry tidak ditemukan) | **PASS** |
| 15 | Multi-block File Allocation | Tulis file berukuran > 6 KiB (menggunakan Single Indirect block) | Return code 0, seluruh partisi blok terisi urut tanpa data korup | **PASS** |
| 16 | Persistensi Data Disk | Reboot QEMU dan baca kembali data disk | Data tetap terbaca utuh tanpa formatting ulang (`is_empty_storage() == false`) | **PASS** |

Seluruh 16 pengujian berhasil dilewati (**100% PASS**) tanpa adanya error, crash, maupun memory corruption.

---

## 5. Distribusi Tugas Anggota Kelompok

| Nama Anggota | NIM | Pembagian Tugas & Tanggung Jawab |
|:---|:---:|:---|
| **Ghaniyul Amri Caulava** | 13525106 | Implementasi driver filesystem EXT2 IF2130 Edition (`ext2.c`, `ext2.h`), logika alokasi First-Fit, penanganan indirection inode, dan integrasi repositori |
| **Nabil Rabbani** | 13525004 | Implementasi driver ATA PIO Disk (`disk.c`, `disk.h`), port I/O 16-bit (`portio.c`, `portio.h`), dan otomatisasi disk image Makefile |
| **Raditya Wibian Sastaka** | 13525019 | Implementasi operasi CRUD filesystem (`read`, `write`, `read_directory`, `delete`), validasi return code sesuai spesifikasi, dan stack expansion |
| **Mochammad Nuha Al Ghifari** | 13525056 | Perancangan skenario pengujian komprehensif di `kernel.c`, verifikasi data integrity disk biner, dokumentasi jurnal, dan review spesifikasi |

---

## 6. Kesimpulan dan Refleksi

Implementasi Chapter 2 (Milestone 2) telah memenuhi 100% spesifikasi teknis dan batasan yang tercantum pada dokumen panduan praktikum IF2130 Sister Lab ITB. Sistem operasi kini memiliki kapabilitas persistensi sekunder berkinerja tinggi, modular, dan siap digunakan sebagai fondasi pemuatan executable biner (User Mode Shell) pada Milestone 3 berikutnya.
