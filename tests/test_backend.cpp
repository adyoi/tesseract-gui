/*
 * test_backend.cpp — headless tests for every operation the GUI can perform.
 *
 * These run without a display: if a test here passes, the same call from a
 * dialog produces the same result.  Covered: key generation (plain and
 * passphrase locked), public key recovery, file encryption in binary and
 * armored form, multi-recipient messages, passphrase encryption, detached
 * signatures, signer pinning, text mode, inspection and the helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include "backend/tessbackend.h"

#include <tesseract/tesseract.h>

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace tessgui;

namespace {

/**
 * Run the command line tool.  Returns true only for a clean exit; the
 * combined output is handed back for the failure message.
 */
bool runCli(const QString &cli, const QStringList &args, QString *output = nullptr) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(cli, args);
    if (!process.waitForStarted(10000)) {
        if (output) *output = QStringLiteral("could not start %1").arg(cli);
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(5000);
        if (output) *output = QStringLiteral("timed out: %1 %2").arg(cli, args.join(QLatin1Char(' ')));
        return false;
    }
    if (output) *output = QString::fromUtf8(process.readAll());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} /* namespace */

class TestBackend : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    static bool writeFile(const QString &file, const QByteArray &data) {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(data) == data.size();
    }

    static QByteArray readFile(const QString &file) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return QByteArray();
        return f.readAll();
    }

    /** Deterministic "binary" content: contains NUL bytes and every value. */
    static QByteArray sample(int size) {
        QByteArray data(size, Qt::Uninitialized);
        for (int i = 0; i < size; ++i) data[i] = static_cast<char>((i * 31 + 7) % 256);
        return data;
    }

    /** Generates a key pair and returns the public key path. */
    QString makeKey(const QString &name, bool locked = false,
                    const QString &passphrase = QString()) {
        const KeygenResult result = keygen(path(name), locked, passphrase, 0, 0);
        if (!result.ok) {
            qWarning("keygen failed: %s", qPrintable(result.message));
            return QString();
        }
        return result.publicPath;
    }

private slots:
    void initTestCase();
    void libraryReportsVersions();
    void keygenCreatesBothKeys();
    void lockedKeyNeedsItsPassphrase();
    void fileRoundTrip();
    void armoredFileRoundTrip();
    void multiRecipientMessage();
    void messageForAnotherKeyIsRejected();
    void passphraseModeRoundTrip();
    void detachedSignature();
    void requiredSignerPolicy();
    void textModeRoundTrip();
    void inspectionReportsTheHeader();
    void helpersBehave();
    void badArgumentsAreReported();
    void cliInteroperability();
};

void TestBackend::initTestCase() {
    QVERIFY2(m_dir.isValid(), "cannot create a temporary directory");
}

void TestBackend::libraryReportsVersions() {
    const QString version = libraryVersion();
    QVERIFY(version.contains(QStringLiteral("libtesseract_crypt")));
    QVERIFY(version.contains(QStringLiteral("libsodium")));
}

void TestBackend::keygenCreatesBothKeys() {
    const QString base = path(QStringLiteral("gen"));
    const KeygenResult result = keygen(base, false, QString(), 0, 0);
    QVERIFY2(result.ok, qPrintable(result.message));
    QCOMPARE(result.privatePath, base + QStringLiteral(".key"));
    QCOMPARE(result.publicPath, base + QStringLiteral(".pub"));
    QCOMPARE(result.fingerprint.size(), static_cast<int>(TESS_FINGERPRINT_HEX));
    QVERIFY(QFile::exists(result.privatePath));
    QVERIFY(QFile::exists(result.publicPath));

    const KeyResult info = keyInfo(result.privatePath, QString());
    QVERIFY2(info.ok, qPrintable(info.message));
    QVERIFY(info.isSecret);
    QVERIFY(!info.isLocked);
    QCOMPARE(info.fingerprint, result.fingerprint);

    /* The public key must report the same fingerprint. */
    const KeyResult pubInfo = keyInfo(result.publicPath, QString());
    QVERIFY2(pubInfo.ok, qPrintable(pubInfo.message));
    QVERIFY(!pubInfo.isSecret);
    QCOMPARE(pubInfo.fingerprint, result.fingerprint);

    /* pubkey: derive the public half from the private file */
    const QString derived = path(QStringLiteral("gen-derived.pub"));
    const KeyResult exported = exportPublicKey(result.privatePath, QString(), derived);
    QVERIFY2(exported.ok, qPrintable(exported.message));
    QCOMPARE(exported.fingerprint, result.fingerprint);
    QCOMPARE(readFile(derived), readFile(result.publicPath));
}

void TestBackend::lockedKeyNeedsItsPassphrase() {
    const QString base = path(QStringLiteral("locked"));
    const QString passphrase = QStringLiteral("correct horse battery staple");
    QVERIFY(!makeKey(QStringLiteral("locked"), true, passphrase).isEmpty());

    /* without a passphrase the key cannot be read */
    const KeyResult noPass = keyInfo(base + QStringLiteral(".key"), QString());
    QVERIFY(!noPass.ok);
    QVERIFY(noPass.needsPassphrase);
    QVERIFY(!noPass.wrongPassphrase);

    /* a wrong passphrase is flagged as such */
    const KeyResult wrong = keyInfo(base + QStringLiteral(".key"), QStringLiteral("nope"));
    QVERIFY(!wrong.ok);
    QVERIFY(wrong.needsPassphrase);
    QVERIFY(wrong.wrongPassphrase);

    /* the right one unlocks it */
    const KeyResult good = keyInfo(base + QStringLiteral(".key"), passphrase);
    QVERIFY2(good.ok, qPrintable(good.message));
    QVERIFY(good.isSecret);
    QCOMPARE(good.fingerprint.size(), static_cast<int>(TESS_FINGERPRINT_HEX));

    /* the public half is readable without any passphrase */
    const KeyResult pub = keyInfo(base + QStringLiteral(".pub"), QString());
    QVERIFY2(pub.ok, qPrintable(pub.message));
    QVERIFY(!pub.isSecret);
    QCOMPARE(pub.fingerprint, good.fingerprint);

    /* exporting from a locked key needs the passphrase, then works */
    const QString out = path(QStringLiteral("locked-derived.pub"));
    const KeyResult denied = exportPublicKey(base + QStringLiteral(".key"), QString(), out);
    QVERIFY(!denied.ok);
    QVERIFY(denied.needsPassphrase);
    const KeyResult allowed =
        exportPublicKey(base + QStringLiteral(".key"), passphrase, out);
    QVERIFY2(allowed.ok, qPrintable(allowed.message));
    QCOMPARE(allowed.fingerprint, good.fingerprint);
}

void TestBackend::fileRoundTrip() {
    /* 1 MiB crosses the default 64 KiB chunk boundary many times. */
    const QByteArray plain = sample(1024 * 1024);
    const QString source = path(QStringLiteral("plain.bin"));
    QVERIFY(writeFile(source, plain));

    const QString recipientKey = makeKey(QStringLiteral("pair-a"));
    QVERIFY(!recipientKey.isEmpty());
    const QString privateKey = path(QStringLiteral("pair-a.key"));

    const QString sealed = path(QStringLiteral("plain.bin.enc"));
    const Result encrypted =
        sealFile(source, sealed, {recipientKey}, QString(), QString(), false, false,
                 QString(), 0, false);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));
    QVERIFY(QFile::exists(sealed));
    /* the stream starts with the format magic, so it is not stored in clear */
    QCOMPARE(readFile(sealed).left(4), QByteArray("TSCR"));

    const QByteArray cipher = readFile(sealed);
    QVERIFY(cipher.size() > plain.size());

    const QString opened = path(QStringLiteral("plain.bin.out"));
    const OpenResult decrypted = openFile(sealed, opened, privateKey, QString(), QString(),
                                          false, QString());
    QVERIFY2(decrypted.ok, qPrintable(decrypted.message));
    QVERIFY(!decrypted.signedFlag);
    QCOMPARE(readFile(opened), plain);

    /* a corrupted ciphertext must not produce output at all */
    QByteArray damaged = cipher;
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 0xff);
    const QString damagedPath = path(QStringLiteral("plain.bin.damaged"));
    QVERIFY(writeFile(damagedPath, damaged));
    const QString rejected = path(QStringLiteral("plain.bin.rejected"));
    const OpenResult failed =
        openFile(damagedPath, rejected, privateKey, QString(), QString(), false, QString());
    QVERIFY(!failed.ok);
    QCOMPARE(failed.status, static_cast<int>(TESS_ERR_CRYPTO));
    QVERIFY(!QFile::exists(rejected));
}

void TestBackend::armoredFileRoundTrip() {
    const QByteArray plain = sample(4096);
    const QString source = path(QStringLiteral("armor.txt"));
    QVERIFY(writeFile(source, plain));
    const QString recipientKey = makeKey(QStringLiteral("pair-armor"));
    const QString privateKey = path(QStringLiteral("pair-armor.key"));

    const QString sealed = path(QStringLiteral("armor.txt.enc"));
    const Result encrypted = sealFile(source, sealed, {recipientKey}, QString(), QString(),
                                      false, false, QString(), 0, true);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));
    const QByteArray armored = readFile(sealed);
    QVERIFY(armored.startsWith("-----BEGIN TESSERACT MESSAGE-----"));
    QVERIFY(armored.contains("-----END TESSERACT MESSAGE-----"));

    /* decrypting an armored file must work without any extra step */
    const QString opened = path(QStringLiteral("armor.txt.out"));
    const OpenResult decrypted = openFile(sealed, opened, privateKey, QString(), QString(),
                                          false, QString());
    QVERIFY2(decrypted.ok, qPrintable(decrypted.message));
    QCOMPARE(readFile(opened), plain);
}

void TestBackend::multiRecipientMessage() {
    const QString keyA = makeKey(QStringLiteral("multi-a"));
    const QString keyB = makeKey(QStringLiteral("multi-b"));
    QVERIFY(!keyA.isEmpty() && !keyB.isEmpty());

    const QByteArray plain = sample(70000); /* two chunks and a bit */
    const QString source = path(QStringLiteral("multi.bin"));
    QVERIFY(writeFile(source, plain));
    const QString sealed = path(QStringLiteral("multi.bin.enc"));

    const Result encrypted = sealFile(source, sealed, {keyA, keyB}, QString(), QString(),
                                      false, false, QString(), 0, false);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));

    const InspectResult info = inspectFile(sealed);
    QVERIFY2(info.ok, qPrintable(info.message));
    QCOMPARE(info.version, 2);
    QCOMPARE(info.mode, static_cast<int>(TESS_MODE_PUBLICKEY));
    QCOMPARE(static_cast<int>(info.recipientCount), 2);
    QVERIFY(info.lengthKnown);
    QCOMPARE(info.plaintextLen, static_cast<quint64>(plain.size()));

    for (const QString &key : {QStringLiteral("multi-a.key"), QStringLiteral("multi-b.key")}) {
        const QString opened = path(key + QStringLiteral(".out"));
        const OpenResult decrypted = openFile(sealed, opened, path(key), QString(), QString(),
                                              false, QString());
        QVERIFY2(decrypted.ok, qPrintable(decrypted.message));
        QCOMPARE(readFile(opened), plain);
    }
}

void TestBackend::messageForAnotherKeyIsRejected() {
    const QString keyA = makeKey(QStringLiteral("stranger-a"));
    const QString keyB = makeKey(QStringLiteral("stranger-b"));
    const QByteArray plain = sample(2048);
    const QString source = path(QStringLiteral("stranger.txt"));
    QVERIFY(writeFile(source, plain));
    const QString sealed = path(QStringLiteral("stranger.txt.enc"));
    QVERIFY(sealFile(source, sealed, {keyA}, QString(), QString(), false, false, QString(), 0,
                     false)
                .ok);

    const QString opened = path(QStringLiteral("stranger.out"));
    const OpenResult result = openFile(sealed, opened, path(QStringLiteral("stranger-b.key")),
                                       QString(), QString(), false, QString());
    QVERIFY(!result.ok);
    QCOMPARE(result.status, static_cast<int>(TESS_ERR_RECIPIENT));
    QVERIFY(!QFile::exists(opened));
}

void TestBackend::passphraseModeRoundTrip() {
    const QString passphrase = QStringLiteral("secret-passphrase");
    const QByteArray plain = sample(5000);
    const QString source = path(QStringLiteral("pass.bin"));
    QVERIFY(writeFile(source, plain));
    const QString sealed = path(QStringLiteral("pass.bin.enc"));

    const Result encrypted = sealFile(source, sealed, {}, QString(), QString(), false, true,
                                      passphrase, 0, false);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));

    const InspectResult info = inspectFile(sealed);
    QVERIFY2(info.ok, qPrintable(info.message));
    QCOMPARE(info.mode, static_cast<int>(TESS_MODE_PASSPHRASE));
    QCOMPARE(static_cast<int>(info.recipientCount), 0);

    const QString opened = path(QStringLiteral("pass.bin.out"));
    const OpenResult decrypted = openFile(sealed, opened, QString(), QString(), QString(), true,
                                          passphrase);
    QVERIFY2(decrypted.ok, qPrintable(decrypted.message));
    QCOMPARE(readFile(opened), plain);

    const QString wrongOut = path(QStringLiteral("pass.bin.wrong"));
    const OpenResult wrong = openFile(sealed, wrongOut, QString(), QString(), QString(), true,
                                      QStringLiteral("not-the-passphrase"));
    QVERIFY(!wrong.ok);
    QVERIFY(!wrong.message.isEmpty());
    QVERIFY(!QFile::exists(wrongOut));

    /* both a key and a passphrase at once is a usage error */
    const OpenResult both = openFile(sealed, path(QStringLiteral("pass.bin.both")),
                                     path(QStringLiteral("pair-a.key")), QString(), QString(),
                                     true, passphrase);
    QVERIFY(!both.ok);
    QCOMPARE(both.status, static_cast<int>(TESS_ERR_INVALID_ARG));
}

void TestBackend::detachedSignature() {
    const QString publicKey = makeKey(QStringLiteral("signer"));
    const QString privateKey = path(QStringLiteral("signer.key"));
    QVERIFY(!publicKey.isEmpty());

    const QByteArray data = sample(12345);
    const QString file = path(QStringLiteral("signed.bin"));
    QVERIFY(writeFile(file, data));

    const QString signature = path(QStringLiteral("signed.bin.sig"));
    const Result signed_ = signFile(file, privateKey, QString(), signature);
    QVERIFY2(signed_.ok, qPrintable(signed_.message));
    const QByteArray armored = readFile(signature);
    QVERIFY(armored.startsWith("-----BEGIN TESSERACT SIGNATURE-----"));

    const Result verified = verifyFile(file, signature, publicKey);
    QVERIFY2(verified.ok, qPrintable(verified.message));

    /* changing one byte of the file must break the signature */
    QByteArray tampered = data;
    tampered[10] = static_cast<char>(tampered[10] ^ 0x01);
    const QString tamperedFile = path(QStringLiteral("signed.tampered"));
    QVERIFY(writeFile(tamperedFile, tampered));
    const Result broken = verifyFile(tamperedFile, signature, publicKey);
    QVERIFY(!broken.ok);
    QCOMPARE(broken.status, static_cast<int>(TESS_ERR_SIGNATURE));

    /* a locked key asks for its passphrase before signing */
    const QString lockedBase = path(QStringLiteral("signer-locked"));
    const QString passphrase = QStringLiteral("sign-me");
    QVERIFY(!makeKey(QStringLiteral("signer-locked"), true, passphrase).isEmpty());
    const QString lockedSig = path(QStringLiteral("locked.bin.sig"));
    const Result needsPass = signFile(file, lockedBase + QStringLiteral(".key"), QString(), lockedSig);
    QVERIFY(!needsPass.ok);
    QVERIFY(needsPass.needsPassphrase);
    const Result shipped =
        signFile(file, lockedBase + QStringLiteral(".key"), passphrase, lockedSig);
    QVERIFY2(shipped.ok, qPrintable(shipped.message));
    QVERIFY(verifyFile(file, lockedSig, lockedBase + QStringLiteral(".pub")).ok);
}

void TestBackend::requiredSignerPolicy() {
    const QString senderKey = makeKey(QStringLiteral("policy-sender"));
    const QString otherKey = makeKey(QStringLiteral("policy-other"));
    QVERIFY(!senderKey.isEmpty() && !otherKey.isEmpty());

    const QByteArray plain = sample(3000);
    const QString source = path(QStringLiteral("policy.bin"));
    QVERIFY(writeFile(source, plain));

    /* 1) signed by the sender: accepted, and the signature is reported */
    const QString signedMessage = path(QStringLiteral("policy-signed.enc"));
    const Result encrypted = sealFile(source, signedMessage, {senderKey},
                                      path(QStringLiteral("policy-sender.key")), QString(),
                                      true, false, QString(), 0, false);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));
    const QString opened = path(QStringLiteral("policy-signed.out"));
    const OpenResult ok = openFile(signedMessage, opened, path(QStringLiteral("policy-sender.key")),
                                   QString(), senderKey, false, QString());
    QVERIFY2(ok.ok, qPrintable(ok.message));
    QVERIFY(ok.signedFlag);
    QCOMPARE(readFile(opened), plain);

    /* 2) pinned to a different signer: refused */
    const QString refused = path(QStringLiteral("policy-refused.out"));
    const OpenResult wrongSigner =
        openFile(signedMessage, refused, path(QStringLiteral("policy-sender.key")), QString(),
                 otherKey, false, QString());
    QVERIFY(!wrongSigner.ok);
    QCOMPARE(wrongSigner.status, static_cast<int>(TESS_ERR_SIGNER));
    QVERIFY(!QFile::exists(refused));

    /* 3) unsigned message with a required signer: refused, output removed */
    const QString plainMessage = path(QStringLiteral("policy-plain.enc"));
    QVERIFY(sealFile(source, plainMessage, {senderKey}, QString(), QString(), false, false,
                     QString(), 0, false)
                .ok);
    const QString dropped = path(QStringLiteral("policy-dropped.out"));
    const OpenResult unsigned_ =
        openFile(plainMessage, dropped, path(QStringLiteral("policy-sender.key")), QString(),
                 senderKey, false, QString());
    QVERIFY(!unsigned_.ok);
    QCOMPARE(unsigned_.status, static_cast<int>(TESS_ERR_SIGNATURE));
    QVERIFY(!QFile::exists(dropped));

    /* 4) without the policy the same message decrypts fine and is reported
     *    as unsigned */
    const QString recovered = path(QStringLiteral("policy-recovered.out"));
    const OpenResult loose = openFile(plainMessage, recovered,
                                      path(QStringLiteral("policy-sender.key")), QString(),
                                      QString(), false, QString());
    QVERIFY2(loose.ok, qPrintable(loose.message));
    QVERIFY(!loose.signedFlag);
    QCOMPARE(readFile(recovered), plain);
}

void TestBackend::textModeRoundTrip() {
    const QString text = QStringLiteral("rahasia: { \"nasi\" : \"goreng\" } \u2014 em dash");
    const QString recipientKey = makeKey(QStringLiteral("text-key"));
    const QString privateKey = path(QStringLiteral("text-key.key"));
    QVERIFY(!recipientKey.isEmpty());

    const TextResult encrypted =
        sealText(text, {recipientKey}, QString(), QString(), false, false, QString(), 0);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));
    QVERIFY(encrypted.text.startsWith(QStringLiteral("-----BEGIN TESSERACT MESSAGE-----")));

    const TextResult decrypted = openText(encrypted.text, privateKey, QString(), QString(),
                                          false, QString());
    QVERIFY2(decrypted.ok, qPrintable(decrypted.message));
    QCOMPARE(decrypted.text, text);

    /* passphrase flavour */
    const TextResult symmetric =
        sealText(text, {}, QString(), QString(), false, true, QStringLiteral("pw"), 0);
    QVERIFY2(symmetric.ok, qPrintable(symmetric.message));
    const TextResult back = openText(symmetric.text, QString(), QString(), QString(), true,
                                     QStringLiteral("pw"));
    QVERIFY2(back.ok, qPrintable(back.message));
    QCOMPARE(back.text, text);

    const TextResult badPass = openText(symmetric.text, QString(), QString(), QString(), true,
                                        QStringLiteral("wrong"));
    QVERIFY(!badPass.ok);

    /* a locked key must be reported so the dialog can ask for the passphrase */
    const QString lockedPass = QStringLiteral("locked-text");
    QVERIFY(!makeKey(QStringLiteral("locked-text"), true, lockedPass).isEmpty());
    const TextResult locked =
        sealText(text, {path(QStringLiteral("locked-text.pub"))}, QString(), QString(), false,
                 false, QString(), 0);
    QVERIFY2(locked.ok, qPrintable(locked.message));
    const TextResult needs =
        openText(locked.text, path(QStringLiteral("locked-text.key")), QString(), QString(),
                 false, QString());
    QVERIFY(!needs.ok);
    QVERIFY(needs.needsPassphrase);
    QVERIFY(!needs.wrongPassphrase);
    const TextResult unlocked =
        openText(locked.text, path(QStringLiteral("locked-text.key")), lockedPass, QString(),
                 false, QString());
    QVERIFY2(unlocked.ok, qPrintable(unlocked.message));
    QCOMPARE(unlocked.text, text);

    /* damaged armor is reported, not crashed on */
    QString broken = encrypted.text;
    broken.replace(QStringLiteral("A"), QStringLiteral("B"));
    const TextResult damaged = openText(broken, privateKey, QString(), QString(), false,
                                        QString());
    QVERIFY(!damaged.ok);
}

void TestBackend::inspectionReportsTheHeader() {
    const QString recipientKey = makeKey(QStringLiteral("inspect-key"));
    const QByteArray plain = sample(9000);
    const QString source = path(QStringLiteral("inspect.bin"));
    QVERIFY(writeFile(source, plain));

    const QString sealed = path(QStringLiteral("inspect.bin.enc"));
    QVERIFY(sealFile(source, sealed, {recipientKey}, QString(), QString(), false, false,
                     QString(), 8192, false)
                .ok);

    const InspectResult info = inspectFile(sealed);
    QVERIFY2(info.ok, qPrintable(info.message));
    QCOMPARE(info.version, 1);            /* one recipient -> format v1 */
    QCOMPARE(info.suite, 1);
    QCOMPARE(info.mode, static_cast<int>(TESS_MODE_PUBLICKEY));
    QCOMPARE(static_cast<int>(info.recipientCount), 1);
    QCOMPARE(static_cast<int>(info.chunkSize), 8192);
    QVERIFY(!info.signedFlag);
    QVERIFY(info.lengthKnown);
    QCOMPARE(info.plaintextLen, static_cast<quint64>(plain.size()));

    /* an armored file is unwrapped before parsing */
    const QString armored = path(QStringLiteral("inspect.armored"));
    QVERIFY(sealFile(source, armored, {recipientKey}, QString(), QString(), false, false,
                     QString(), 0, true)
                .ok);
    QVERIFY(inspectFile(armored).ok);

    /* something that is not a message is rejected with a clear status */
    const QString other = path(QStringLiteral("not-a-message.bin"));
    QVERIFY(writeFile(other, sample(600)));
    const InspectResult bad = inspectFile(other);
    QVERIFY(!bad.ok);
    QCOMPARE(bad.status, static_cast<int>(TESS_ERR_FORMAT));
    QVERIFY(!inspectFile(path(QStringLiteral("missing.bin"))).ok);
}

void TestBackend::helpersBehave() {
    QCOMPARE(suggestEncryptedName(QStringLiteral("a.txt")), QStringLiteral("a.txt.enc"));
    QCOMPARE(suggestDecryptedName(QStringLiteral("a.txt.enc")), QStringLiteral("a.txt"));
    QCOMPARE(suggestDecryptedName(QStringLiteral("a.txt")), QStringLiteral("a.txt.decrypted"));
    QCOMPARE(suggestSignatureName(QStringLiteral("a.txt")), QStringLiteral("a.txt.sig"));
    QCOMPARE(suggestKeyBaseName(QStringLiteral("keys/me.key")), QStringLiteral("keys/me"));
    QCOMPARE(suggestKeyBaseName(QStringLiteral("keys/me.pub")), QStringLiteral("keys/me"));
    QCOMPARE(suggestKeyBaseName(QStringLiteral("keys/me")), QStringLiteral("keys/me"));
    QCOMPARE(humanSize(512), QStringLiteral("512 B"));
    QCOMPARE(humanSize(1024), QStringLiteral("1.0 KiB"));
    QCOMPARE(humanSize(1536), QStringLiteral("1.5 KiB"));
    QVERIFY(statusText(TESS_ERR_SIGNER).contains(QStringLiteral("sign")));
}

void TestBackend::badArgumentsAreReported() {
    const QString source = path(QStringLiteral("args.bin"));
    QVERIFY(writeFile(source, sample(64)));
    const QString recipientKey = makeKey(QStringLiteral("args-key"));

    /* no recipient and no passphrase */
    Result noTarget = sealFile(source, path(QStringLiteral("args.enc")), {}, QString(),
                               QString(), false, false, QString(), 0, false);
    QVERIFY(!noTarget.ok);
    QCOMPARE(noTarget.status, static_cast<int>(TESS_ERR_INVALID_ARG));

    /* both at once */
    Result both = sealFile(source, path(QStringLiteral("args2.enc")), {recipientKey}, QString(),
                           QString(), false, true, QStringLiteral("pw"), 0, false);
    QVERIFY(!both.ok);
    QCOMPARE(both.status, static_cast<int>(TESS_ERR_INVALID_ARG));

    /* chunk size outside the range the format allows */
    Result badChunk = sealFile(source, path(QStringLiteral("args3.enc")), {recipientKey},
                               QString(), QString(), false, false, QString(), 123, false);
    QVERIFY(!badChunk.ok);
    QVERIFY(badChunk.message.contains(QStringLiteral("4096")));

    /* missing files */
    Result missing = sealFile(path(QStringLiteral("nope.bin")), path(QStringLiteral("out.enc")),
                              {recipientKey}, QString(), QString(), false, false, QString(), 0,
                              false);
    QVERIFY(!missing.ok);
    QCOMPARE(missing.status, static_cast<int>(TESS_ERR_IO));

    /* an empty passphrase is refused before touching the file system */
    Result emptyPass = sealFile(source, path(QStringLiteral("args4.enc")), {}, QString(),
                                QString(), false, true, QString(), 0, false);
    QVERIFY(!emptyPass.ok);

    /* decrypting without a key and without a passphrase */
    OpenResult noKey = openFile(path(QStringLiteral("args.enc")), path(QStringLiteral("args.out")),
                                QString(), QString(), QString(), false, QString());
    QVERIFY(!noKey.ok);
    QCOMPARE(noKey.status, static_cast<int>(TESS_ERR_INVALID_ARG));
}

/**
 * Keys and messages must be exchangeable with the command line tool.  This
 * test drives the real binary in both directions; it is skipped unless
 * TESSGUI_CLI points at it, because the GUI build does not need the CLI.
 */
void TestBackend::cliInteroperability() {
    const QString cli = qEnvironmentVariable("TESSGUI_CLI");
    if (cli.isEmpty() || !QFileInfo::exists(cli)) {
        QSKIP("set TESSGUI_CLI=<tesseract-crypt binary> to run the interop test");
    }

    const QByteArray plain = sample(4096);
    const QString source = path(QStringLiteral("interop.bin"));
    QVERIFY(writeFile(source, plain));

    QString output;
    const QString base = path(QStringLiteral("interop"));

    /* the CLI produces the key pair, the GUI uses it */
    QVERIFY2(runCli(cli, {QStringLiteral("keygen"), QStringLiteral("-o"), base}, &output),
             qPrintable(output));
    QVERIFY(QFile::exists(base + QStringLiteral(".pub")));
    QVERIFY(QFile::exists(base + QStringLiteral(".key")));

    /* GUI encrypt -> CLI decrypt */
    const QString sealed = path(QStringLiteral("interop-gui.enc"));
    const Result encrypted = sealFile(source, sealed, {base + QStringLiteral(".pub")},
                                      QString(), QString(), false, false, QString(), 0, false);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.message));
    const QString decryptedByCli = path(QStringLiteral("interop-cli.out"));
    QVERIFY2(runCli(cli, {QStringLiteral("decrypt"), QStringLiteral("-k"),
                          base + QStringLiteral(".key"), QStringLiteral("-i"), sealed,
                          QStringLiteral("-o"), decryptedByCli}, &output),
             qPrintable(output));
    QCOMPARE(readFile(decryptedByCli), plain);

    /* the CLI reads the header the GUI wrote */
    QVERIFY2(runCli(cli, {QStringLiteral("inspect"), QStringLiteral("-i"), sealed}, &output),
             qPrintable(output));
    QVERIFY(output.contains(QStringLiteral("format version : 1")));
    QVERIFY(output.contains(QStringLiteral("mode           : public-key")));
    QVERIFY(output.contains(QStringLiteral("recipients     : 1")));
    QVERIFY(output.contains(QStringLiteral("signed         : no")));

    /* CLI encrypt (armored) -> GUI decrypt */
    const QString sealedByCli = path(QStringLiteral("interop-cli.enc"));
    QVERIFY2(runCli(cli, {QStringLiteral("encrypt"), QStringLiteral("-r"),
                          base + QStringLiteral(".pub"), QStringLiteral("-i"), source,
                          QStringLiteral("-o"), sealedByCli, QStringLiteral("--armor")},
                    &output),
             qPrintable(output));
    QVERIFY(readFile(sealedByCli).startsWith(QByteArray("-----BEGIN TESSERACT MESSAGE-----")));
    const QString decryptedByGui = path(QStringLiteral("interop-gui.out"));
    const OpenResult opened = openFile(sealedByCli, decryptedByGui,
                                       base + QStringLiteral(".key"), QString(), QString(),
                                       false, QString());
    QVERIFY2(opened.ok, qPrintable(opened.message));
    QCOMPARE(readFile(decryptedByGui), plain);

    /* a message signed by the CLI is accepted by the GUI, and the other way */
    const QString cliSignature = path(QStringLiteral("interop-cli.sig"));
    QVERIFY2(runCli(cli, {QStringLiteral("sign"), QStringLiteral("-k"),
                          base + QStringLiteral(".key"), QStringLiteral("-i"), source,
                          QStringLiteral("-o"), cliSignature},
                    &output),
             qPrintable(output));
    QVERIFY(verifyFile(source, cliSignature, base + QStringLiteral(".pub")).ok);

    const QString guiSignature = path(QStringLiteral("interop-gui.sig"));
    const Result signedByGui = signFile(source, base + QStringLiteral(".key"), QString(),
                                        guiSignature);
    QVERIFY2(signedByGui.ok, qPrintable(signedByGui.message));
    QVERIFY2(runCli(cli, {QStringLiteral("verify"), QStringLiteral("-k"),
                          base + QStringLiteral(".pub"), QStringLiteral("-i"), source,
                          QStringLiteral("-s"), guiSignature},
                    &output),
             qPrintable(output));

    /* string mode: armored by both sides, read by both sides */
    const QString cliText = path(QStringLiteral("interop-cli.txt"));
    QVERIFY2(runCli(cli, {QStringLiteral("encrypt"), QStringLiteral("--text"),
                          QStringLiteral("halo dari CLI"), QStringLiteral("-r"),
                          base + QStringLiteral(".pub"), QStringLiteral("--armor"),
                          QStringLiteral("-o"), cliText},
                    &output),
             qPrintable(output));
    const TextResult fromCli = openText(QString::fromUtf8(readFile(cliText)),
                                        base + QStringLiteral(".key"), QString(), QString(),
                                        false, QString());
    QVERIFY2(fromCli.ok, qPrintable(fromCli.message));
    QCOMPARE(fromCli.text, QStringLiteral("halo dari CLI"));

    const TextResult toCli =
        sealText(QStringLiteral("halo dari GUI"), {base + QStringLiteral(".pub")}, QString(),
                 QString(), false, false, QString(), 0);
    QVERIFY2(toCli.ok, qPrintable(toCli.message));
    const QString guiText = path(QStringLiteral("interop-gui.txt"));
    QVERIFY(writeFile(guiText, toCli.text.toUtf8()));
    const QString textOut = path(QStringLiteral("interop-text.out"));
    QVERIFY2(runCli(cli, {QStringLiteral("decrypt"), QStringLiteral("-k"),
                          base + QStringLiteral(".key"), QStringLiteral("-i"), guiText,
                          QStringLiteral("-o"), textOut},
                    &output),
             qPrintable(output));
    QCOMPARE(QString::fromUtf8(readFile(textOut)), QStringLiteral("halo dari GUI"));
}

QTEST_GUILESS_MAIN(TestBackend)
#include "test_backend.moc"
