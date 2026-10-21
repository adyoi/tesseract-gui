# Changelog

Semua perubahan penting pada Tesseract Crypt GUI dicatat di sini, mengikuti
[Keep a Changelog](https://keepachangelog.com/id-ID/1.1.0/) dan
[Semantic Versioning](https://semver.org/lang/id/).

## [Unreleased]

## [1.0.0] — 2026-10-21

Rilis pertama.

### Ditambahkan

- **Antarmuka Qt6 Widgets** untuk `libtesseract_crypt`, sesuai sketsa: menu
  File | Edit | Tools | Help, pemilih mode Encrypt/Decrypt, baris Source,
  Destination, Key Public, dan Key Private (hanya di mode Decrypt), tombol
  Keygen, serta status bar.
- **Enkripsi berkas** dengan satu atau banyak kunci publik penerima (format v1
  untuk satu penerima, v2 untuk beberapa), tanda tangan pengirim opsional,
  keluaran biner atau armored, dan ukuran chunk yang bisa diatur.
- **Dekripsi berkas** dengan kunci privat (termasuk kunci terkunci
  passphrase) atau mode passphrase, deteksi armor otomatis, keluaran atomik,
  dan kebijakan *require signer*: bila pesan tidak bertanda tangan, berkas
  hasil dihapus, bukan ditinggalkan.
- **Dialog Keygen** dengan passphrase dan parameter Argon2id (`--kdf-ops`,
  `--kdf-mem`), menampilkan sidik jari kunci yang baru dibuat.
- **Menu Tools**: Public key from private key, Sign a file (`.sig` armored),
  Verify a signature, Inspect a message (header: versi, suite, mode, jumlah
  penerima, status tanda tangan, ukuran chunk, panjang plaintext), Key
  information (jenis kunci + sidik jari), Encrypt text, dan Decrypt text.
- **Dialog Options** (Edit → Options) yang menyimpan default lewat QSettings:
  armor, ukuran chunk, kunci pengirim untuk tanda tangan, *require signer*,
  parameter KDF untuk kunci baru, dan konfirmasi tumpa berkas.
- **Operasi di latar belakang**: enkripsi/dekripsi berjalan di thread pekerja
  dengan dialog progres, sehingga jendela tidak membeku untuk berkas besar.
- **Prompt passphrase berulang**: kunci terkunci dikenali dari
  `TESS_ERR_PASSPHRASE`, GUI menanyakan passphrase dan mencoba ulang (hingga
  tiga kali), sama seperti prompt CLI.
- **Drag & drop** berkas ke jendela serta saran nama berkas tujuan otomatis
  (`sumber.enc`, atau nama tanpa sufiks saat dekripsi).
- **Uji headless** (`tests/test_backend.cpp`) yang menjalankan fungsi backend
  yang sama dengan antarmuka: keygen terkunci, pubkey, putaran penuh berkas
  1 MiB, armored, multi-penerima, passphrase, tanda tangan + pemalsuan,
  kebijakan penanda tangan, mode teks, inspeksi, dan penolakan argumen.
- **Mode tangkapan layar** (`--screenshot`, `--dialog`, `--mode`) untuk
  merender jendela ke PNG di lingkungan tanpa layar; dipakai uji CI dan
  dokumentasi.
- **Ikon program** dari artwork `tesseract-crypt` yang sama: logo pada jendela
  dan dialog About, serta berkas `.ico` (7 ukuran) yang tertanam di executable
  lewat berkas sumber Windows. Ikon dapat dibuat ulang dengan
  `tesseract-gui --export-icon assets/tesseract-gui.ico`.
- **Uji interoperabilitas CLI**: dengan `TESSGUI_CLI` menunjuk biner
  `tesseract-crypt`, uji membuffer kunci dan pesan dua arah — GUI mengenkripsi
  lalu CLI mendekripsi, CLI mengenkripsi (armor) lalu GUI mendekripsi,
  tanda tangan ditukar dua arah, dan mode teks ditukar dua arah. Lewati
  (`QSKIP`) bila biner CLI tidak ada.
- **Folder build yang berdiri sendiri** di Windows: `windeployqt6` membawa
  pustaka Qt dan plugin-nya, `cmake/collect_runtime.cmake` menyalin seluruh
  rantai dependensi (ICU, freetype, harfbuzz, brotli, zstd, runtime MinGW)
  ke sebelah executable, sehingga program berjalan tanpa menyiapkan `PATH`.

### Catatan

- Di Windows, libsodium ditautkan dinamis (DLL dibundel di sebelah `.exe`),
  sedangkan rilis CLI menautkannya statis. Format berkas dan kunci identik.
- Ikon jendela mengikuti tema gelap/terang sistem; logo memakai warna
  gradien yang sama dengan logo `tesseract-crypt`.
