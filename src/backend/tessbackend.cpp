/*
 * tessbackend.cpp — implementation of the GUI's use of libtesseract_crypt.
 *
 * Behaviour mirrors the tesseract-crypt CLI: same defaults, same format
 * choices (v1 for one recipient, v2 for several), same armored labels, same
 * atomic write-then-rename policy.
 *
 * SPDX-License-Identifier: MIT
 */
#include "backend/tessbackend.h"

#include <sodium.h>
#include <tesseract/tesseract.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>
#include <vector>

namespace tessgui {

namespace {

constexpr const char *kMessageLabel = "TESSERACT MESSAGE";
constexpr const char *kSignatureLabel = "TESSERACT SIGNATURE";

/** Owning pointer for tess_key (the C API has no RAII of its own). */
struct KeyPtr {
    tess_key *key = nullptr;
    ~KeyPtr() { if (key) tess_key_free(key); }
    KeyPtr() = default;
    KeyPtr(const KeyPtr &) = delete;
    KeyPtr &operator=(const KeyPtr &) = delete;
    KeyPtr(KeyPtr &&other) noexcept : key(other.key) { other.key = nullptr; }
    KeyPtr &operator=(KeyPtr &&other) noexcept {
        if (this != &other) {
            if (key) tess_key_free(key);
            key = other.key;
            other.key = nullptr;
        }
        return *this;
    }
    tess_key **out() { return &key; }
    tess_key *get() const { return key; }
    operator bool() const { return key != nullptr; }
};

/** Owning pointer for the malloc'd buffers the library returns. */
template <typename T>
struct BufPtr {
    T *data = nullptr;
    ~BufPtr() { if (data) tess_free(data); }
    T **out() { return &data; }
    const T *get() const { return data; }
    BufPtr() = default;
    BufPtr(const BufPtr &) = delete;
    BufPtr &operator=(const BufPtr &) = delete;
};

QByteArray utf8(const QString &s) { return s.toUtf8(); }
QString fromUtf8(const char *s) { return QString::fromUtf8(s ? s : ""); }

/** Pass the passphrase as a NUL terminated string, or NULL when empty. */
const char *passArg(const QByteArray &pass) {
    return pass.isEmpty() ? nullptr : pass.constData();
}

bool readFile(const QString &path, QByteArray *out, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return false;
    }
    *out = f.readAll();
    if (f.error() != QFileDevice::NoError) {
        *error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

/**
 * Write through a temporary file and rename, so a failed write never leaves a
 * half finished destination behind (the library does the same for its own
 * file APIs).
 */
bool writeFileAtomic(const QString &path, const QByteArray &data, QString *error) {
    const QString part = path + QStringLiteral(".part");
    {
        QFile f(part);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *error = QStringLiteral("cannot write %1: %2").arg(part, f.errorString());
            return false;
        }
        if (f.write(data) != data.size() || !f.flush()) {
            *error = QStringLiteral("cannot write %1: %2").arg(part, f.errorString());
            f.close();
            QFile::remove(part);
            return false;
        }
    }
    if (QFile::exists(path) && !QFile::remove(path)) {
        *error = QStringLiteral("cannot replace %1").arg(path);
        QFile::remove(part);
        return false;
    }
    if (!QFile::rename(part, path)) {
        *error = QStringLiteral("cannot rename %1 to %2").arg(part, path);
        QFile::remove(part);
        return false;
    }
    return true;
}

/** Chunk size is either "default" (0) or inside the range the format allows. */
bool checkChunkSize(quint32 chunkSize, Result *bad) {
    if (chunkSize == 0) return true;
    if (chunkSize >= TESS_MIN_CHUNK && chunkSize <= TESS_MAX_CHUNK) return true;
    *bad = Result::failure(static_cast<int>(TESS_ERR_INVALID_ARG));
    bad->message = QStringLiteral("chunk size must be between %1 and %2 bytes")
                       .arg(TESS_MIN_CHUNK)
                       .arg(TESS_MAX_CHUNK);
    return false;
}

void setPassphraseHints(Result *r, bool hadPassphrase) {
    if (r->status != TESS_ERR_PASSPHRASE) return;
    r->needsPassphrase = true;
    r->wrongPassphrase = hadPassphrase;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Result helpers                                                      */
/* ------------------------------------------------------------------ */

Result Result::failure(int status) {
    Result r;
    r.ok = false;
    r.status = status;
    r.message = statusText(status);
    return r;
}

Result Result::success(const QString &message) {
    Result r;
    r.ok = true;
    r.status = TESS_OK;
    r.message = message;
    return r;
}

/* ------------------------------------------------------------------ */
/* Static metadata                                                     */
/* ------------------------------------------------------------------ */

QString libraryVersion() {
    return QStringLiteral("libtesseract_crypt %1 (libsodium %2)")
        .arg(fromUtf8(tess_version()), fromUtf8(sodium_version_string()));
}

QString humanSize(quint64 bytes) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return u == 0 ? QStringLiteral("%1 B").arg(bytes)
                  : QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(units[u]);
}

QString statusText(int status) {
    switch (status) {
    case TESS_OK:
        return QStringLiteral("success");
    case TESS_ERR_INVALID_ARG:
        return QStringLiteral("invalid argument");
    case TESS_ERR_NOMEM:
        return QStringLiteral("out of memory");
    case TESS_ERR_IO:
        return QStringLiteral("input/output error");
    case TESS_ERR_FORMAT:
        return QStringLiteral("not a tesseract message (bad format or truncated)");
    case TESS_ERR_CRYPTO:
        return QStringLiteral("authentication failed: wrong key or passphrase, or the file was modified");
    case TESS_ERR_PASSPHRASE:
        return QStringLiteral("the key is locked (or the passphrase is wrong)");
    case TESS_ERR_SIGNATURE:
        return QStringLiteral("signature verification failed");
    case TESS_ERR_SIGNER:
        return QStringLiteral("the message was signed by a different key");
    case TESS_ERR_UNSUPPORTED:
        return QStringLiteral("unsupported format version or option");
    case TESS_ERR_INTERNAL:
        return QStringLiteral("internal library error");
    case TESS_ERR_RECIPIENT:
        return QStringLiteral("this message is not addressed to that key");
    default:
        return fromUtf8(tess_strerror(static_cast<tess_status>(status)));
    }
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                        */
/* ------------------------------------------------------------------ */

QString suggestEncryptedName(const QString &source) {
    if (source.isEmpty()) return source;
    return source + QStringLiteral(".enc");
}

QString suggestDecryptedName(const QString &source) {
    if (source.isEmpty()) return source;
    static const QStringList suffixes = {QStringLiteral(".enc"), QStringLiteral(".tscr"),
                                         QStringLiteral(".tess"), QStringLiteral(".asc")};
    for (const QString &suffix : suffixes) {
        if (source.endsWith(suffix, Qt::CaseInsensitive)) {
            return source.left(source.size() - suffix.size());
        }
    }
    return source + QStringLiteral(".decrypted");
}

QString suggestSignatureName(const QString &source) {
    if (source.isEmpty()) return source;
    return source + QStringLiteral(".sig");
}

QString suggestKeyBaseName(const QString &source) {
    QString base = source;
    if (base.endsWith(QStringLiteral(".key"), Qt::CaseInsensitive)) {
        base.chop(4);
    } else if (base.endsWith(QStringLiteral(".pub"), Qt::CaseInsensitive)) {
        base.chop(4);
    }
    return base;
}

/* ------------------------------------------------------------------ */
/* Keygen                                                              */
/* ------------------------------------------------------------------ */

KeygenResult keygen(const QString &basePath, bool lock, const QString &passphrase,
                    quint32 argonOps, quint64 argonMemBytes) {
    KeygenResult out;
    const QString base = suggestKeyBaseName(basePath);
    if (base.isEmpty()) {
        out = KeygenResult();
        out.ok = false;
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose a name for the key pair");
        return out;
    }
    if (lock && passphrase.isEmpty()) {
        out.ok = false;
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("the passphrase is empty");
        return out;
    }

    const QByteArray pass = utf8(passphrase);
    KeyPtr secret, pub;
    tess_status st = lock ? tess_keygen_locked(passArg(pass), argonOps, argonMemBytes,
                                               secret.out(), pub.out())
                          : tess_keygen(secret.out(), pub.out());
    if (st != TESS_OK) {
        KeygenResult r;
        r.ok = false;
        r.status = st;
        r.message = statusText(st);
        setPassphraseHints(&r, false);
        return r;
    }

    const QString privPath = base + QStringLiteral(".key");
    const QString pubPath = base + QStringLiteral(".pub");
    st = tess_key_save(secret.get(), utf8(privPath).constData());
    if (st == TESS_OK) {
        st = tess_key_save(pub.get(), utf8(pubPath).constData());
    }
    if (st != TESS_OK) {
        KeygenResult r;
        r.ok = false;
        r.status = st;
        r.message = QStringLiteral("saving the keys failed: %1").arg(statusText(st));
        return r;
    }

    out.ok = true;
    out.status = TESS_OK;
    out.privatePath = privPath;
    out.publicPath = pubPath;
    char fp[TESS_FINGERPRINT_HEX + 1] = {0};
    if (tess_key_fingerprint(pub.get(), fp) == TESS_OK) {
        out.fingerprint = fromUtf8(fp);
    }
    out.message = QStringLiteral("key pair written: %1 / %2")
                      .arg(QFileInfo(privPath).fileName(), QFileInfo(pubPath).fileName());
    return out;
}

/* ------------------------------------------------------------------ */
/* Key inspection                                                      */
/* ------------------------------------------------------------------ */

namespace {

/** Load a key file, reporting a locked key as "ask for a passphrase". */
tess_status loadKey(const QString &path, const QString &passphrase, KeyPtr *key,
                    bool *needsPassphrase, bool *wrongPassphrase) {
    const QByteArray pass = utf8(passphrase);
    tess_status st = tess_key_load(utf8(path).constData(), passArg(pass), key->out());
    if (st == TESS_ERR_PASSPHRASE) {
        *needsPassphrase = true;
        *wrongPassphrase = !pass.isEmpty();
    }
    return st;
}

} /* namespace */

KeyResult exportPublicKey(const QString &keyFile, const QString &passphrase,
                          const QString &outFile) {
    KeyResult out;
    bool needs = false, wrong = false;
    KeyPtr key;
    tess_status st = loadKey(keyFile, passphrase, &key, &needs, &wrong);
    if (st != TESS_OK) {
        out.ok = false;
        out.status = st;
        out.message = QStringLiteral("loading %1 failed: %2")
                          .arg(QFileInfo(keyFile).fileName(), statusText(st));
        out.needsPassphrase = needs;
        out.wrongPassphrase = wrong;
        return out;
    }

    KeyPtr pub;
    st = tess_key_get_public(key.get(), pub.out());
    if (st != TESS_OK) {
        out.ok = false;
        out.status = st;
        out.message = QStringLiteral("deriving the public key failed: %1").arg(statusText(st));
        return out;
    }

    st = tess_key_save(pub.get(), utf8(outFile).constData());
    if (st != TESS_OK) {
        out.ok = false;
        out.status = st;
        out.message = QStringLiteral("saving %1 failed: %2").arg(outFile, statusText(st));
        return out;
    }

    out.ok = true;
    out.status = TESS_OK;
    char fp[TESS_FINGERPRINT_HEX + 1] = {0};
    if (tess_key_fingerprint(pub.get(), fp) == TESS_OK) {
        out.fingerprint = fromUtf8(fp);
    }
    out.message = QStringLiteral("public key written to %1").arg(QFileInfo(outFile).fileName());
    return out;
}

KeyResult keyInfo(const QString &keyFile, const QString &passphrase) {
    KeyResult out;
    bool needs = false, wrong = false;
    KeyPtr key;
    tess_status st = loadKey(keyFile, passphrase, &key, &needs, &wrong);
    if (st != TESS_OK) {
        out.ok = false;
        out.status = st;
        out.message = QStringLiteral("loading %1 failed: %2")
                          .arg(QFileInfo(keyFile).fileName(), statusText(st));
        out.needsPassphrase = needs;
        out.wrongPassphrase = wrong;
        return out;
    }
    char fp[TESS_FINGERPRINT_HEX + 1] = {0};
    if (tess_key_fingerprint(key.get(), fp) != TESS_OK) {
        out.ok = false;
        out.status = TESS_ERR_INTERNAL;
        out.message = QStringLiteral("cannot read the key fingerprint");
        return out;
    }
    out.ok = true;
    out.status = TESS_OK;
    out.fingerprint = fromUtf8(fp);
    out.isSecret = tess_key_is_secret(key.get()) != 0;
    out.isLocked = tess_key_is_locked(key.get()) != 0;
    return out;
}

/* ------------------------------------------------------------------ */
/* PE split: large files as a sequence of encrypted chunks            */
/* ------------------------------------------------------------------ */
/* The container format lives in src/core/tess_pe.c (libtesseract_crypt);
 * this file only adapts it to the GUI's Result/OpenResult types.        */

/** True when the file starts with the "PE" marker. */
bool isPeFile(const QString &path) {
    return tess_is_pe_file(utf8(path).constData()) != 0;
}

/** Public wrapper: does this file carry the "PE" marker? */
bool isPeContainer(const QString &path) { return isPeFile(path); }

Result sealFilePe(const QString &inFile, const QString &outFile,
                  const QStringList &recipients, const QString &senderKeyFile,
                  const QString &senderPassphrase, bool sign, bool usePassphrase,
                  const QString &passphrase, quint32 chunkSize, bool armor,
                  quint32 peChunkMiB) {
    Result bad;
    if (!checkChunkSize(chunkSize, &bad)) return bad;
    if (recipients.isEmpty() && !usePassphrase) {
        return Result::failure(TESS_ERR_INVALID_ARG);
    }
    if (!recipients.isEmpty() && usePassphrase) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("choose either recipient keys or a passphrase, not both");
        return r;
    }
    if (usePassphrase && passphrase.isEmpty()) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("the passphrase is empty");
        return r;
    }

    QFileInfo finfo(inFile);
    if (!finfo.exists() || !finfo.isFile()) {
        Result r = Result::failure(TESS_ERR_IO);
        r.message = QStringLiteral("cannot read %1").arg(inFile);
        return r;
    }

    std::vector<KeyPtr> owned;
    owned.reserve(static_cast<size_t>(recipients.size()));
    std::vector<const tess_key *> views;
    for (const QString &path : recipients) {
        KeyPtr k;
        const QByteArray p = utf8(path);
        tess_status st = tess_key_load(p.constData(), nullptr, k.out());
        if (st != TESS_OK) {
            Result r;
            r.ok = false;
            r.status = st;
            r.message = QStringLiteral("loading recipient %1 failed: %2")
                            .arg(QFileInfo(path).fileName(), statusText(st));
            return r;
        }
        owned.push_back(std::move(k));
        views.push_back(owned.back().get());
    }

    KeyPtr sender;
    if (!senderKeyFile.isEmpty() && !owned.empty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(senderKeyFile, senderPassphrase, &sender, &needs, &wrong);
        if (st != TESS_OK) {
            Result r;
            r.ok = false;
            r.status = st;
            r.message = QStringLiteral("loading sender key %1 failed: %2")
                            .arg(QFileInfo(senderKeyFile).fileName(), statusText(st));
            r.needsPassphrase = needs;
            r.wrongPassphrase = wrong;
            return r;
        }
        if (tess_key_is_locked(sender.get())) {
            Result r = Result::failure(TESS_ERR_PASSPHRASE);
            r.message = QStringLiteral("the sender key is still locked");
            r.needsPassphrase = true;
            return r;
        }
    }
    if (sign && !sender) sign = false;

    const QByteArray pass = utf8(passphrase);
    tess_seal_file_options opt;
    tess_seal_file_options_init(&opt);
    if (views.size() == 1) {
        opt.recipient_public = views.front();
    } else if (!views.empty()) {
        opt.recipients = views.data();
        opt.recipient_count = views.size();
    }
    opt.sender_secret = sender.get();
    opt.sign = sign ? 1 : 0;
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.chunk_size = chunkSize;

    tess_pe_options pe;
    tess_pe_options_init(&pe);
    pe.chunk_bytes = static_cast<uint64_t>(peChunkMiB) * 1024ull * 1024ull;
    pe.armor = armor ? 1 : 0;

    tess_status st = tess_seal_file_pe(utf8(inFile).constData(),
                                       utf8(outFile).constData(), &opt, &pe);
    if (st != TESS_OK) {
        Result r;
        r.ok = false;
        r.status = st;
        r.message = QStringLiteral("encrypting %1 failed: %2")
                        .arg(QFileInfo(inFile).fileName(), statusText(st));
        return r;
    }
    return Result::success(QStringLiteral("encrypted to %1").arg(QFileInfo(outFile).fileName()));
}

OpenResult openFilePe(const QString &inFile, const QString &outFile,
                       const QString &keyFile, const QString &keyPassphrase,
                       const QString &requiredSignerFile, bool usePassphrase,
                       const QString &passphrase) {
    OpenResult out;

    if (keyFile.isEmpty() && !usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose a private key or a passphrase");
        return out;
    }
    if (!keyFile.isEmpty() && usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose either a private key or a passphrase, not both");
        return out;
    }
    if (usePassphrase && passphrase.isEmpty()) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("the passphrase is empty");
        return out;
    }

    KeyPtr secret;
    if (!keyFile.isEmpty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(keyFile, keyPassphrase, &secret, &needs, &wrong);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading %1 failed: %2")
                              .arg(QFileInfo(keyFile).fileName(), statusText(st));
            out.needsPassphrase = needs;
            out.wrongPassphrase = wrong;
            return out;
        }
        if (tess_key_is_locked(secret.get())) {
            out.status = TESS_ERR_PASSPHRASE;
            out.message = QStringLiteral("the key is still locked");
            out.needsPassphrase = true;
            return out;
        }
    }

    KeyPtr signer;
    if (!requiredSignerFile.isEmpty()) {
        const QByteArray p = utf8(requiredSignerFile);
        tess_status st = tess_key_load(p.constData(), nullptr, signer.out());
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading signer %1 failed: %2")
                              .arg(QFileInfo(requiredSignerFile).fileName(), statusText(st));
            return out;
        }
    }

    const QByteArray pass = utf8(passphrase);
    int signedFlag = 0;
    tess_open_file_options opt;
    tess_open_file_options_init(&opt);
    opt.recipient_secret = secret.get();
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.required_signer = signer.get();
    opt.out_signed = &signedFlag;

    tess_status st = tess_open_file_pe(utf8(inFile).constData(), utf8(outFile).constData(), &opt);
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("decrypting %1 failed: %2")
                          .arg(QFileInfo(inFile).fileName(), statusText(st));
        setPassphraseHints(&out, false);
        return out;
    }

    out.ok = true;
    out.status = TESS_OK;
    out.signedFlag = signedFlag != 0;
    out.outPath = outFile;
    if (out.signedFlag) {
        out.message = QStringLiteral("decrypted to %1 — signature OK")
                          .arg(QFileInfo(outFile).fileName());
    } else if (!requiredSignerFile.isEmpty()) {
        QFile::remove(outFile);
        out.ok = false;
        out.status = TESS_ERR_SIGNATURE;
        out.outPath.clear();
        out.message = QStringLiteral("the message is not signed — the output file was removed");
        return out;
    } else {
        out.message = QStringLiteral("decrypted to %1 (message was not signed)")
                          .arg(QFileInfo(outFile).fileName());
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Encrypt                                                             */
/* ------------------------------------------------------------------ */

Result sealFile(const QString &inFile, const QString &outFile,
                const QStringList &recipients, const QString &senderKeyFile,
                const QString &senderPassphrase, bool sign, bool usePassphrase,
                const QString &passphrase, quint32 chunkSize, bool armor) {
    Result bad;
    if (!checkChunkSize(chunkSize, &bad)) return bad;
    if (recipients.isEmpty() && !usePassphrase) {
        return Result::failure(TESS_ERR_INVALID_ARG);
    }
    if (!recipients.isEmpty() && usePassphrase) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("choose either recipient keys or a passphrase, not both");
        return r;
    }
    if (usePassphrase && passphrase.isEmpty()) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("the passphrase is empty");
        return r;
    }

    std::vector<KeyPtr> owned;
    owned.reserve(static_cast<size_t>(recipients.size()));
    std::vector<const tess_key *> views;
    for (const QString &path : recipients) {
        KeyPtr k;
        const QByteArray p = utf8(path);
        tess_status st = tess_key_load(p.constData(), nullptr, k.out());
        if (st != TESS_OK) {
            Result r;
            r.ok = false;
            r.status = st;
            r.message = QStringLiteral("loading recipient %1 failed: %2")
                            .arg(QFileInfo(path).fileName(), statusText(st));
            return r;
        }
        owned.push_back(std::move(k));
        views.push_back(owned.back().get());
    }

    KeyPtr sender;
    if (!senderKeyFile.isEmpty() && !owned.empty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(senderKeyFile, senderPassphrase, &sender, &needs, &wrong);
        if (st != TESS_OK) {
            Result r;
            r.ok = false;
            r.status = st;
            r.message = QStringLiteral("loading sender key %1 failed: %2")
                            .arg(QFileInfo(senderKeyFile).fileName(), statusText(st));
            r.needsPassphrase = needs;
            r.wrongPassphrase = wrong;
            return r;
        }
        if (tess_key_is_locked(sender.get())) {
            Result r = Result::failure(TESS_ERR_PASSPHRASE);
            r.message = QStringLiteral("the sender key is still locked");
            r.needsPassphrase = true;
            return r;
        }
    }
    if (sign && !sender) sign = false; /* signing needs a sender key */

    const QByteArray pass = utf8(passphrase);
    const QByteArray in = utf8(inFile);
    const QByteArray out = utf8(outFile);

    /* Armored output has to be assembled in memory first: the streaming file
     * API only writes the binary frame (same trade-off as the CLI). */
    if (!armor) {
        tess_seal_file_options opt;
        tess_seal_file_options_init(&opt);
        if (views.size() == 1) {
            opt.recipient_public = views.front();
        } else if (!views.empty()) {
            opt.recipients = views.data();
            opt.recipient_count = views.size();
        }
        opt.sender_secret = sender.get();
        opt.sign = sign ? 1 : 0;
        opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
        opt.chunk_size = chunkSize;
        tess_status st = tess_seal_file(in.constData(), out.constData(), &opt);
        if (st != TESS_OK) {
            Result r;
            r.ok = false;
            r.status = st;
            r.message = QStringLiteral("encrypting %1 failed: %2")
                            .arg(QFileInfo(inFile).fileName(), statusText(st));
            return r;
        }
        Result r = Result::success(QStringLiteral("encrypted to %1")
                                       .arg(QFileInfo(outFile).fileName()));
        r.message = QStringLiteral("encrypted to %1").arg(QFileInfo(outFile).fileName());
        return r;
    }

    QByteArray plain;
    QString ioError;
    if (!readFile(inFile, &plain, &ioError)) {
        Result r = Result::failure(TESS_ERR_IO);
        r.message = ioError;
        return r;
    }

    tess_seal_options opt;
    tess_seal_options_init(&opt);
    if (views.size() == 1) {
        opt.recipient_public = views.front();
    } else if (!views.empty()) {
        opt.recipients = views.data();
        opt.recipient_count = views.size();
    }
    opt.sender_secret = sender.get();
    opt.sign = sign ? 1 : 0;
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.chunk_size = chunkSize;

    BufPtr<uint8_t> cipher;
    size_t cipherLen = 0;
    tess_status st = tess_seal(reinterpret_cast<const uint8_t *>(plain.constData()),
                               static_cast<size_t>(plain.size()), &opt,
                               cipher.out(), &cipherLen);
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("encrypting %1 failed: %2")
                        .arg(QFileInfo(inFile).fileName(), statusText(st));
        return r;
    }

    BufPtr<char> armored;
    st = tess_armor(cipher.get(), cipherLen, kMessageLabel, armored.out());
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("armor failed: %1").arg(statusText(st));
        return r;
    }

    if (!writeFileAtomic(outFile, QByteArray(armored.get()), &ioError)) {
        Result r = Result::failure(TESS_ERR_IO);
        r.message = ioError;
        return r;
    }
    return Result::success(QStringLiteral("encrypted to %1").arg(QFileInfo(outFile).fileName()));
}

TextResult sealText(const QString &text, const QStringList &recipients,
                    const QString &senderKeyFile, const QString &senderPassphrase,
                    bool sign, bool usePassphrase, const QString &passphrase,
                    quint32 chunkSize) {
    TextResult out;
    Result bad;
    if (!checkChunkSize(chunkSize, &bad)) {
        out.ok = bad.ok;
        out.status = bad.status;
        out.message = bad.message;
        return out;
    }
    if (recipients.isEmpty() && !usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose a recipient public key or a passphrase");
        return out;
    }
    if (!recipients.isEmpty() && usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose either recipient keys or a passphrase, not both");
        return out;
    }

    std::vector<KeyPtr> owned;
    std::vector<const tess_key *> views;
    for (const QString &path : recipients) {
        KeyPtr k;
        const QByteArray p = utf8(path);
        tess_status st = tess_key_load(p.constData(), nullptr, k.out());
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading recipient %1 failed: %2")
                              .arg(QFileInfo(path).fileName(), statusText(st));
            return out;
        }
        owned.push_back(std::move(k));
        views.push_back(owned.back().get());
    }

    KeyPtr sender;
    if (!senderKeyFile.isEmpty() && !owned.empty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(senderKeyFile, senderPassphrase, &sender, &needs, &wrong);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading sender key %1 failed: %2")
                              .arg(QFileInfo(senderKeyFile).fileName(), statusText(st));
            out.needsPassphrase = needs;
            out.wrongPassphrase = wrong;
            return out;
        }
    }
    if (sign && !sender) sign = false;

    tess_seal_options opt;
    tess_seal_options_init(&opt);
    if (views.size() == 1) {
        opt.recipient_public = views.front();
    } else if (!views.empty()) {
        opt.recipients = views.data();
        opt.recipient_count = views.size();
    }
    opt.sender_secret = sender.get();
    opt.sign = sign ? 1 : 0;
    const QByteArray pass = utf8(passphrase);
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.chunk_size = chunkSize;

    const QByteArray pt = utf8(text);
    BufPtr<uint8_t> cipher;
    size_t cipherLen = 0;
    tess_status st = tess_seal(reinterpret_cast<const uint8_t *>(pt.constData()),
                               static_cast<size_t>(pt.size()), &opt, cipher.out(),
                               &cipherLen);
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("encryption failed: %1").arg(statusText(st));
        return out;
    }

    BufPtr<char> armored;
    st = tess_armor(cipher.get(), cipherLen, kMessageLabel, armored.out());
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("armor failed: %1").arg(statusText(st));
        return out;
    }

    out.ok = true;
    out.status = TESS_OK;
    out.text = fromUtf8(armored.get());
    out.message = QStringLiteral("encrypted %1 into %2 of armored text")
                      .arg(humanSize(static_cast<quint64>(pt.size())),
                           humanSize(static_cast<quint64>(out.text.size())));
    return out;
}

/* ------------------------------------------------------------------ */
/* Decrypt                                                             */
/* ------------------------------------------------------------------ */

OpenResult openFile(const QString &inFile, const QString &outFile,
                    const QString &keyFile, const QString &keyPassphrase,
                    const QString &requiredSignerFile, bool usePassphrase,
                    const QString &passphrase) {
    OpenResult out;

    /* A PE container announces itself with the "PE" marker: the decrypt
     * side recognises it and unpacks the chunks. */
    if (isPeFile(inFile)) {
        return openFilePe(inFile, outFile, keyFile, keyPassphrase,
                          requiredSignerFile, usePassphrase, passphrase);
    }

    if (keyFile.isEmpty() && !usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose a private key or a passphrase");
        return out;
    }
    if (!keyFile.isEmpty() && usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose either a private key or a passphrase, not both");
        return out;
    }
    if (usePassphrase && passphrase.isEmpty()) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("the passphrase is empty");
        return out;
    }

    KeyPtr secret;
    if (!keyFile.isEmpty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(keyFile, keyPassphrase, &secret, &needs, &wrong);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading %1 failed: %2")
                              .arg(QFileInfo(keyFile).fileName(), statusText(st));
            out.needsPassphrase = needs;
            out.wrongPassphrase = wrong;
            return out;
        }
        if (tess_key_is_locked(secret.get())) {
            out.status = TESS_ERR_PASSPHRASE;
            out.message = QStringLiteral("the key is still locked");
            out.needsPassphrase = true;
            return out;
        }
    }

    KeyPtr signer;
    if (!requiredSignerFile.isEmpty()) {
        const QByteArray p = utf8(requiredSignerFile);
        tess_status st = tess_key_load(p.constData(), nullptr, signer.out());
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading signer %1 failed: %2")
                              .arg(QFileInfo(requiredSignerFile).fileName(), statusText(st));
            return out;
        }
    }

    const QByteArray pass = utf8(passphrase);
    int signedFlag = 0;
    tess_open_file_options opt;
    tess_open_file_options_init(&opt);
    opt.recipient_secret = secret.get();
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.required_signer = signer.get();
    opt.out_signed = &signedFlag;

    tess_status st = tess_open_file(utf8(inFile).constData(), utf8(outFile).constData(), &opt);
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("decrypting %1 failed: %2")
                          .arg(QFileInfo(inFile).fileName(), statusText(st));
        setPassphraseHints(&out, false);
        return out;
    }

    out.ok = true;
    out.status = TESS_OK;
    out.signedFlag = signedFlag != 0;
    out.outPath = outFile;
    if (out.signedFlag) {
        out.message = QStringLiteral("decrypted to %1 — signature OK")
                          .arg(QFileInfo(outFile).fileName());
    } else if (!requiredSignerFile.isEmpty()) {
        /* Keeping the payload of a message that failed the signature policy
         * would defeat the point of the policy, so the output is dropped. */
        QFile::remove(outFile);
        out.ok = false;
        out.status = TESS_ERR_SIGNATURE;
        out.outPath.clear();
        out.message = QStringLiteral("the message is not signed — the output file was removed");
        return out;
    } else {
        out.message = QStringLiteral("decrypted to %1 (message was not signed)")
                          .arg(QFileInfo(outFile).fileName());
    }
    return out;
}

TextResult openText(const QString &input, const QString &keyFile,
                    const QString &keyPassphrase, const QString &requiredSignerFile,
                    bool usePassphrase, const QString &passphrase) {
    TextResult out;
    if (keyFile.isEmpty() && !usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose a private key or a passphrase");
        return out;
    }
    if (!keyFile.isEmpty() && usePassphrase) {
        out.status = TESS_ERR_INVALID_ARG;
        out.message = QStringLiteral("choose either a private key or a passphrase, not both");
        return out;
    }

    KeyPtr secret;
    if (!keyFile.isEmpty()) {
        bool needs = false, wrong = false;
        tess_status st = loadKey(keyFile, keyPassphrase, &secret, &needs, &wrong);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading %1 failed: %2")
                              .arg(QFileInfo(keyFile).fileName(), statusText(st));
            out.needsPassphrase = needs;
            out.wrongPassphrase = wrong;
            return out;
        }
        if (tess_key_is_locked(secret.get())) {
            out.status = TESS_ERR_PASSPHRASE;
            out.message = QStringLiteral("the key is still locked");
            out.needsPassphrase = true;
            return out;
        }
    }

    KeyPtr signer;
    if (!requiredSignerFile.isEmpty()) {
        const QByteArray p = utf8(requiredSignerFile);
        tess_status st = tess_key_load(p.constData(), nullptr, signer.out());
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("loading signer %1 failed: %2")
                              .arg(QFileInfo(requiredSignerFile).fileName(), statusText(st));
            return out;
        }
    }

    QByteArray data = utf8(input);
    if (tess_is_armored(reinterpret_cast<const uint8_t *>(data.constData()),
                        static_cast<size_t>(data.size()))) {
        BufPtr<uint8_t> raw;
        size_t rawLen = 0;
        tess_status st = tess_dearmor(data.constData(), raw.out(), &rawLen);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("armor is damaged: %1").arg(statusText(st));
            return out;
        }
        data = QByteArray(reinterpret_cast<const char *>(raw.get()),
                          static_cast<int>(rawLen));
    }

    int signedFlag = 0;
    tess_open_options opt;
    tess_open_options_init(&opt);
    opt.recipient_secret = secret.get();
    const QByteArray pass = utf8(passphrase);
    opt.passphrase = usePassphrase ? passArg(pass) : nullptr;
    opt.required_signer = signer.get();
    opt.out_signed = &signedFlag;

    BufPtr<uint8_t> plain;
    size_t plainLen = 0;
    tess_status st = tess_open(reinterpret_cast<const uint8_t *>(data.constData()),
                               static_cast<size_t>(data.size()), &opt, plain.out(),
                               &plainLen);
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("decryption failed: %1").arg(statusText(st));
        setPassphraseHints(&out, false);
        return out;
    }

    const QByteArray bytes(reinterpret_cast<const char *>(plain.get()),
                           static_cast<int>(plainLen));
    out.text = QString::fromUtf8(bytes);
    out.ok = true;
    out.status = TESS_OK;
    if (signedFlag) {
        out.message = QStringLiteral("decrypted %1 — signature OK")
                          .arg(humanSize(static_cast<quint64>(plainLen)));
    } else if (!requiredSignerFile.isEmpty()) {
        out.ok = false;
        out.status = TESS_ERR_SIGNATURE;
        out.message = QStringLiteral("the message is not signed");
        return out;
    } else {
        out.message = QStringLiteral("decrypted %1 (message was not signed)")
                          .arg(humanSize(static_cast<quint64>(plainLen)));
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Sign / verify                                                       */
/* ------------------------------------------------------------------ */

Result signFile(const QString &inFile, const QString &keyFile,
                const QString &passphrase, const QString &outFile) {
    if (keyFile.isEmpty()) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("choose the private key to sign with");
        return r;
    }

    bool needs = false, wrong = false;
    KeyPtr secret;
    tess_status st = loadKey(keyFile, passphrase, &secret, &needs, &wrong);
    if (st != TESS_OK) {
        Result r;
        r.ok = false;
        r.status = st;
        r.message = QStringLiteral("loading %1 failed: %2")
                        .arg(QFileInfo(keyFile).fileName(), statusText(st));
        r.needsPassphrase = needs;
        r.wrongPassphrase = wrong;
        return r;
    }
    if (tess_key_is_locked(secret.get())) {
        Result r = Result::failure(TESS_ERR_PASSPHRASE);
        r.message = QStringLiteral("the key is still locked");
        r.needsPassphrase = true;
        return r;
    }

    uint8_t sig[TESS_SIGNATURE_BYTES];
    st = tess_sign_file(utf8(inFile).constData(), secret.get(), sig);
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("signing %1 failed: %2")
                        .arg(QFileInfo(inFile).fileName(), statusText(st));
        return r;
    }

    BufPtr<char> armored;
    st = tess_armor(sig, sizeof sig, kSignatureLabel, armored.out());
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("armor failed: %1").arg(statusText(st));
        return r;
    }

    QString ioError;
    if (!writeFileAtomic(outFile, QByteArray(armored.get()), &ioError)) {
        Result r = Result::failure(TESS_ERR_IO);
        r.message = ioError;
        return r;
    }
    return Result::success(QStringLiteral("signature written to %1")
                               .arg(QFileInfo(outFile).fileName()));
}

Result verifyFile(const QString &inFile, const QString &signatureFile,
                  const QString &keyFile) {
    if (keyFile.isEmpty() || signatureFile.isEmpty() || inFile.isEmpty()) {
        Result r = Result::failure(TESS_ERR_INVALID_ARG);
        r.message = QStringLiteral("choose the file, its signature and the public key");
        return r;
    }

    const QByteArray keyPath = utf8(keyFile);
    KeyPtr key;
    tess_status st = tess_key_load(keyPath.constData(), nullptr, key.out());
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("loading %1 failed: %2")
                        .arg(QFileInfo(keyFile).fileName(), statusText(st));
        return r;
    }

    QByteArray sigData;
    QString ioError;
    if (!readFile(signatureFile, &sigData, &ioError)) {
        Result r = Result::failure(TESS_ERR_IO);
        r.message = ioError;
        return r;
    }

    BufPtr<uint8_t> sig;
    size_t sigLen = 0;
    st = tess_dearmor(sigData.constData(), sig.out(), &sigLen);
    if (st != TESS_OK || sigLen != TESS_SIGNATURE_BYTES) {
        Result r = Result::failure(st == TESS_OK ? TESS_ERR_FORMAT : st);
        r.message = QStringLiteral("%1 is not a tesseract signature")
                        .arg(QFileInfo(signatureFile).fileName());
        return r;
    }

    st = tess_verify_file(utf8(inFile).constData(), key.get(), sig.get());
    if (st != TESS_OK) {
        Result r = Result::failure(st);
        r.message = QStringLiteral("verification failed: %1").arg(statusText(st));
        return r;
    }
    return Result::success(QStringLiteral("signature is valid for %1")
                               .arg(QFileInfo(inFile).fileName()));
}

/* ------------------------------------------------------------------ */
/* Inspect                                                             */
/* ------------------------------------------------------------------ */

InspectResult inspectFile(const QString &inFile) {
    InspectResult out;
    QByteArray data;
    QString ioError;
    if (!readFile(inFile, &data, &ioError)) {
        out.status = TESS_ERR_IO;
        out.message = ioError;
        return out;
    }
    if (data.isEmpty()) {
        out.status = TESS_ERR_FORMAT;
        out.message = QStringLiteral("%1 is empty").arg(QFileInfo(inFile).fileName());
        return out;
    }

    if (tess_is_armored(reinterpret_cast<const uint8_t *>(data.constData()),
                        static_cast<size_t>(data.size()))) {
        BufPtr<uint8_t> raw;
        size_t rawLen = 0;
        tess_status st = tess_dearmor(data.constData(), raw.out(), &rawLen);
        if (st != TESS_OK) {
            out.status = st;
            out.message = QStringLiteral("armor is damaged: %1").arg(statusText(st));
            return out;
        }
        data = QByteArray(reinterpret_cast<const char *>(raw.get()),
                          static_cast<int>(rawLen));
    }

    tess_message_info info;
    tess_status st = tess_inspect(reinterpret_cast<const uint8_t *>(data.constData()),
                                  static_cast<size_t>(data.size()), &info);
    if (st != TESS_OK) {
        out.status = st;
        out.message = QStringLiteral("%1 is not a tesseract message: %2")
                          .arg(QFileInfo(inFile).fileName(), statusText(st));
        return out;
    }

    out.ok = true;
    out.status = TESS_OK;
    out.version = info.version;
    out.suite = info.suite;
    out.mode = info.mode;
    out.flags = info.flags;
    out.chunkSize = info.chunk_size;
    out.recipientCount = info.recipient_count;
    out.hasSender = info.has_sender != 0;
    out.signedFlag = info.signed_flag != 0;
    out.lengthKnown = info.plaintext_len != std::numeric_limits<quint64>::max();
    out.plaintextLen = out.lengthKnown ? static_cast<quint64>(info.plaintext_len) : 0;
    out.senderFingerprint = fromUtf8(info.fingerprint);
    out.message = QStringLiteral("format v%1, %2, %3 recipient(s)")
                      .arg(info.version)
                      .arg(info.mode == TESS_MODE_PUBLICKEY ? QStringLiteral("public-key")
                                                            : QStringLiteral("passphrase"))
                      .arg(info.recipient_count);
    return out;
}

} /* namespace tessgui */
