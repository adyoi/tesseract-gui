/*
 * tessbackend.h — every libtesseract_crypt operation the GUI can perform.
 *
 * The windows and dialogs never touch the C API directly: they go through
 * these functions.  Keeping the crypto in one place means the same code path
 * is exercised by the GUI and by the headless test suite.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_TESSBACKEND_H
#define TESSGUI_TESSBACKEND_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <limits>

namespace tessgui {

/** Outcome of an operation, plus the detail needed by the interface. */
struct Result {
    bool ok = false;
    int status = 0;            /* tess_status                            */
    QString message;           /* one line for the status bar / a dialog */
    bool needsPassphrase = false; /* key is locked: ask for a passphrase */
    bool wrongPassphrase = false; /* a passphrase was given and rejected */

    explicit operator bool() const { return ok; }

    /** Build a failure from a tess_status code. */
    static Result failure(int status);
    /** Build a success with a summary message. */
    static Result success(const QString &message);
};

struct KeygenResult : Result {
    QString privatePath;
    QString publicPath;
    QString fingerprint;
};

struct KeyResult : Result {
    QString fingerprint;
    bool isSecret = false;
    bool isLocked = false;
};

struct TextResult : Result {
    QString text;
};

struct OpenResult : Result {
    bool signedFlag = false;
    QString outPath;           /* file that received the plaintext */
};

struct InspectResult : Result {
    int version = 0;
    int suite = 0;
    int mode = 0;              /* TESS_MODE_PUBLICKEY / TESS_MODE_PASSPHRASE */
    unsigned flags = 0;
    quint32 chunkSize = 0;
    quint64 plaintextLen = 0;
    bool lengthKnown = false;
    quint32 recipientCount = 0;
    bool hasSender = false;
    bool signedFlag = false;
    QString senderFingerprint;
};

/** Versions of the two libraries this build is linked against. */
QString libraryVersion();

/* ------------------------------------------------------------------ keys */
KeygenResult keygen(const QString &basePath, bool lock, const QString &passphrase,
                    quint32 argonOps, quint64 argonMemBytes);

KeyResult exportPublicKey(const QString &keyFile, const QString &passphrase,
                          const QString &outFile);

KeyResult keyInfo(const QString &keyFile, const QString &passphrase);

/* --------------------------------------------------- encrypt / decrypt  */
Result sealFile(const QString &inFile, const QString &outFile,
                const QStringList &recipients, const QString &senderKeyFile,
                const QString &senderPassphrase, bool sign, bool usePassphrase,
                const QString &passphrase, quint32 chunkSize, bool armor);

TextResult sealText(const QString &text, const QStringList &recipients,
                    const QString &senderKeyFile, const QString &senderPassphrase,
                    bool sign, bool usePassphrase, const QString &passphrase,
                    quint32 chunkSize);

OpenResult openFile(const QString &inFile, const QString &outFile,
                    const QString &keyFile, const QString &keyPassphrase,
                    const QString &requiredSignerFile, bool usePassphrase,
                    const QString &passphrase);

TextResult openText(const QString &input, const QString &keyFile,
                    const QString &keyPassphrase, const QString &requiredSignerFile,
                    bool usePassphrase, const QString &passphrase);

/* --------------------------------------------------- PE split containers */
/**
 * Encrypt a large file as a sequence of sealed chunks.  The container
 * starts with the ASCII marker "PE", so the decrypt side recognises it
 * without an extra flag.
 */
Result sealFilePe(const QString &inFile, const QString &outFile,
                  const QStringList &recipients, const QString &senderKeyFile,
                  const QString &senderPassphrase, bool sign, bool usePassphrase,
                  const QString &passphrase, quint32 chunkSize, bool armor,
                  quint32 peChunkMiB);

/** Decrypt a PE container back into one plain file. */
OpenResult openFilePe(const QString &inFile, const QString &outFile,
                       const QString &keyFile, const QString &keyPassphrase,
                       const QString &requiredSignerFile, bool usePassphrase,
                       const QString &passphrase);

/** True when the file starts with the "PE" marker. */
bool isPeContainer(const QString &path);
/* ------------------------------------------------------ sign / verify   */
Result signFile(const QString &inFile, const QString &keyFile,
                const QString &passphrase, const QString &outFile);

Result verifyFile(const QString &inFile, const QString &signatureFile,
                  const QString &keyFile);

/* ------------------------------------------------------------ metadata  */
InspectResult inspectFile(const QString &inFile);

/* ------------------------------------------------------------- helpers  */
/** Human readable size, e.g. "1.4 MiB". */
QString humanSize(quint64 bytes);
/** Friendly text for a tess_status code (falls back to tess_strerror). */
QString statusText(int status);
/** Source path + ".enc". */
QString suggestEncryptedName(const QString &source);
/** Source without a known ciphertext suffix, else source + ".decrypted". */
QString suggestDecryptedName(const QString &source);
/** Source path + ".sig". */
QString suggestSignatureName(const QString &source);
/** Source without a trailing ".key", used as the keygen basename. */
QString suggestKeyBaseName(const QString &source);

} /* namespace tessgui */

#endif /* TESSGUI_TESSBACKEND_H */
