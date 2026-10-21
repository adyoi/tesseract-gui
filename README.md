# Tesseract Crypt GUI

Antarmuka **Qt6** untuk [`libtesseract_crypt`](https://github.com/adyoi/tesseract-crypt)
— seluruh fitur CLI `tesseract-crypt` tersedia dalam bentuk form: enkripsi dan
dekripsi berkas, enkripsi string, pembuatan kunci, tanda tangan lepas
(*detached signature*), verifikasi, inspeksi pesan, dan info kunci.

GUI ini **tidak menulis kripto sendiri**. Semua operasi memanggil
`libtesseract_crypt` yang sama seperti CLI, jadi berkas, kunci, dan format
armor yang dihasilkan bisa ditukar bolak-balik dengan `tesseract-crypt` tanpa
konversi apa pun.

> **English:** [README.en.md](README.en.md)

## Tangkapan layar

| | |
| --- | --- |
| ![Jendela utama mode Encrypt](docs/main-encrypt.png) | ![Jendela utama mode Decrypt](docs/main-decrypt.png) |
| Mode **Encrypt** | Mode **Decrypt** (baris *Key Private* muncul) |
| ![Dialog Key pair](docs/keygen.png) | ![Dialog Options](docs/options.png) |
| **Tools → Key pair** | **Edit → Options** |
| ![Mode teks](docs/text-encrypt.png) | ![Dialog About](docs/about.png) |
| **Tools → Encrypt text** | **Help → About** |

Logo jendela dan ikon program memakai artwork yang sama dengan
[`tesseract-crypt`](https://github.com/adyoi/tesseract-crypt)
(`docs/assets/logo.svg`, lisensi MIT), jadi keduanya tampil serasi.

## Tata letak

Sesuai sketsa:

```
File  Edit  Tools  Help
────────────────────────────────────────────────────────────
(•) Encrypt        ( ) Decrypt

Source        [_______________________________]  [...]
Destination   [_______________________________]  [...]
Key Public    [_______________________________]  [...]      (mode Encrypt)
Key Private   [_______________________________]  [...]      (mode Decrypt)
[ ] Encrypt with a passphrase  [.....................]

[ Keygen ]                                    [ Encrypt ]
```

| Bagian | Isi |
| --- | --- |
| **File → Exit** | keluar |
| **Edit → Options…** | default armor, ukuran chunk, kunci pengirim (tanda tangan), *require signer*, KDF Argon2id, konfirmasi tumpa berkas |
| **Tools** | Key pair, Public key from private key, Sign a file, Verify a signature, Inspect a message, Key information, Encrypt text, Decrypt text |
| **Help → About** | versi library, libsodium, dan Qt yang dipakai |

Baris **Key Public** menerima lebih dari satu berkas (pisahkan dengan `;` atau
pilih beberapa berkas sekaligus di dialog) — itu cara membuat pesan
multi-penerima format v2. Baris **Key Private** hanya muncul di mode Decrypt,
persis seperti sketsa. Berkas juga bisa di-*drag & drop* ke jendela.

## Padanan fitur dengan CLI

| Perintah / opsi CLI | Padanan di GUI |
| --- | --- |
| `keygen -o NAME` | tombol **Keygen** atau Tools → Key pair |
| `keygen --passphrase` | centang *Protect the private key with a passphrase* di dialog Key pair |
| `keygen --kdf-ops --kdf-mem` | KDF passes / KDF memory di dialog Key pair (default dari Options) |
| `pubkey -k … -o …` | Tools → Public key from private key (bisa memakai kunci terkunci) |
| `encrypt -r A.pub [-r B.pub …]` | mode **Encrypt** + baris **Key Public** (beberapa berkas = format v2) |
| `encrypt -k SENDER.key` | Options → *Sender key* (+ *Sign messages*) |
| `encrypt --no-sign` | matikan *Sign messages* di Options |
| `encrypt --armor` | Options → *Write armored output* |
| `encrypt --chunk-size N` | Options → *Chunk size* (KiB) |
| `encrypt --passphrase` | centang **Encrypt with a passphrase** |
| `encrypt --text "…"` | Tools → Encrypt text |
| `decrypt -k RECIPIENT.key` | mode **Decrypt** + baris **Key Private** |
| `decrypt --require-signer S.pub` | Options → *Only accept messages signed by this key* |
| `decrypt --passphrase` | centang **Decrypt with a passphrase** |
| `sign -k KEY -i FILE` | Tools → Sign a file (`.sig` armored) |
| `verify -k PUB -s SIG -i FILE` | Tools → Verify a signature |
| `inspect -i FILE` | Tools → Inspect a message |
| `info -k KEYFILE` | Tools → Key information |
| `version`, `help` | Help → About |

Kode keluar CLI (0/1/2/3) tidak relevan di GUI; status ditampilkan di status bar
dan, bila gagal, di kotak dialog — pesan `tess_status` yang sama diterjemahkan
ke kalimat yang lebih mudah dibaca.

## Perbedaan perilaku yang disengaja

- **Konfirmasi tumpa**: GUI bertanya sebelum menimpa berkas tujuan (bisa
  dimatikan di Options). Perpindahan nama tetap atomik seperti di CLI.
- **Kebijakan penanda tangan**: bila *require signer* aktif dan pesan ternyata
  tidak bertanda tangan, GUI **menghapus** berkas hasil dekripsi (CLI
  membiarkannya). Isi pesan tetap terautentikasi; yang gagal hanyalah kebijakan
  asal-usul, dan hasilnya tidak seharusnya ditinggalkan begitu saja.
- **Passphrase** hanya hidup di memori selama operasi. Yang disimpan di
  QSettings hanyalah *lokasi* berkas kunci, bukan passphrase.
- Di Windows GUI menautkan libsodium **secara dinamis** (`libsodium-26.dll`
  dibundel di sebelah `.exe`), sedangkan rilis CLI menautkannya statis.

## Membangun

Prasyarat (MSYS2 / MinGW-w64):

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
                  mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6-base \
                  mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-libsodium
```

`qt6-svg` dipakai untuk logo (SVG) pada jendela dan dialog About.

Letakkan pohon `tesseract-crypt` bersebelahan dengan proyek ini, atau pakai
tesseract-crypt yang sudah dipasang (`find_package(tesseract)`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Pilihan lain: `-DTESSGUI_TESSERACT_SOURCE_DIR=<path ke tesseract-crypt>`.
Saat dibangun dari pohon sumber, hanya pustakanya yang dibangun
(`TESS_BUILD_CLI=OFF`, `TESS_BUILD_TESTS=OFF`).

## Menjalankan

```bash
./build/tesseract-gui.exe
```

Di Windows, CMake membuat folder build **berdiri sendiri**: `windeployqt6`
membawa pustaka Qt dan plugin-nya, lalu `cmake/collect_runtime.cmake`
menyalin seluruh rantai dependensi (ICU, freetype, harfbuzz, brotli, zstd,
runtime MinGW) ke sebelah `.exe`. Karena itu program bisa dijalankan tanpa
menyiapkan `PATH`:

```bash
C:\Windows\System32\cmd.exe /c "set PATH=C:\Windows\System32 && build\tesseract-gui.exe"
```

Untuk paket yang bisa dipindah ke mesin lain, jalankan `cmake --install build`
atau salin seluruh isi folder build (pustaka Qt, plugin, dan DLL runtime).

`libsodium-26.dll` sudah otomatis disalin oleh CMake ketika proyek dibangun
dengan import library (`.dll.a`); bila yang ditautkan adalah arsip statis
`libsodium.a`, tidak ada DLL yang perlu dibawa.

## Uji otomatis (tanpa layar)

```bash
ctest --test-dir build --output-on-failure
```

- `backend` — `tests/test_backend.cpp` memanggil **fungsi backend yang sama
  dengan yang dipakai antarmuka**: keygen biasa dan terkunci, pubkey, putaran
  penuh enkripsi/dekripsi berkas 1 MiB, keluaran armored, multi-penerima,
  passphrase, tanda tangan + pemalsuan berkas, kebijakan penanda tangan,
  mode teks, inspeksi header, dan penolakan argumen keliru. Rincian hasil
  ditulis ke `build/tests/backend.log` (Qt Test di Windows tidak menulis ke
  stdout saat keluaran dialihkan).
- `screenshots` — menjalankan aplikasi dengan `QT_QPA_PLATFORM=offscreen` dan
  merender jendela ke PNG; gagal bila jendela tidak bisa dibangun.

Uji interoperabilitas dengan CLI ikut dijalankan bila biner CLI tersedia.
Arahkan variabel lingkungan `TESSGUI_CLI` ke executable `tesseract-crypt`,
lalu uji membuffer kunci dan pesan dua arah (GUI → CLI dan CLI → GUI):

```bash
TESSGUI_CLI=/path/to/tesseract-crypt ctest --test-dir build --output-on-failure
```

Mode tangkapan layar juga bisa dipakai manual:

```bash
QT_QPA_PLATFORM=offscreen ./build/tesseract-gui.exe \
    --screenshot docs/main-encrypt.png --dialog main --mode encrypt
```

`--dialog` menerima `main`, `keygen`, `options`, `pubkey`, `sign`, `verify`,
`inspect`, `info`, `text`, `textdecrypt`, dan `about`.

Ikon program (`.ico`) dibuat dari logo yang sama:

```bash
./build/tesseract-gui.exe --export-icon assets/tesseract-gui.ico
```

## Lisensi

MIT — lihat [LICENSE](LICENSE).
