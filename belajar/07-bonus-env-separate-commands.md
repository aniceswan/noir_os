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
