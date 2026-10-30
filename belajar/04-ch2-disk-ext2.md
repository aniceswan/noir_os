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
