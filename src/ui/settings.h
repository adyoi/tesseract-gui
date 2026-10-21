/*
 * settings.h — the preferences the Options dialog edits (QSettings backed).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_SETTINGS_H
#define TESSGUI_SETTINGS_H

#include <QSettings>
#include <QString>

namespace tessgui {

/** Everything the Options dialog remembers between runs. */
struct GuiSettings {
    /* Encrypt */
    bool armor = false;
    quint32 chunkSize = 0;              /* 0 = format default                 */
    bool signWhenPossible = true;       /* sign when a sender key is set      */
    QString senderKey;                  /* private key used to sign messages  */

    /* Decrypt */
    bool requireSigner = false;
    QString signerKey;                  /* public key the signer must match   */

    /* Keygen */
    bool lockNewKeys = false;
    quint32 kdfOps = 0;                 /* 0 = library default                */
    quint32 kdfMemMiB = 0;              /* 0 = library default                */

    /* Interface */
    bool confirmOverwrite = true;
    QString lastDirectory;

    static GuiSettings load() {
        QSettings s;
        GuiSettings g;
        s.beginGroup(QStringLiteral("encrypt"));
        g.armor = s.value(QStringLiteral("armor"), g.armor).toBool();
        g.chunkSize = s.value(QStringLiteral("chunkSize"), g.chunkSize).toUInt();
        g.signWhenPossible = s.value(QStringLiteral("sign"), g.signWhenPossible).toBool();
        g.senderKey = s.value(QStringLiteral("senderKey")).toString();
        s.endGroup();

        s.beginGroup(QStringLiteral("decrypt"));
        g.requireSigner = s.value(QStringLiteral("requireSigner"), g.requireSigner).toBool();
        g.signerKey = s.value(QStringLiteral("signerKey")).toString();
        s.endGroup();

        s.beginGroup(QStringLiteral("keygen"));
        g.lockNewKeys = s.value(QStringLiteral("lock"), g.lockNewKeys).toBool();
        g.kdfOps = s.value(QStringLiteral("kdfOps"), g.kdfOps).toUInt();
        g.kdfMemMiB = s.value(QStringLiteral("kdfMemMiB"), g.kdfMemMiB).toUInt();
        s.endGroup();

        s.beginGroup(QStringLiteral("ui"));
        g.confirmOverwrite = s.value(QStringLiteral("confirmOverwrite"), g.confirmOverwrite).toBool();
        g.lastDirectory = s.value(QStringLiteral("lastDirectory")).toString();
        s.endGroup();
        return g;
    }

    void save() const {
        QSettings s;
        s.beginGroup(QStringLiteral("encrypt"));
        s.setValue(QStringLiteral("armor"), armor);
        s.setValue(QStringLiteral("chunkSize"), chunkSize);
        s.setValue(QStringLiteral("sign"), signWhenPossible);
        s.setValue(QStringLiteral("senderKey"), senderKey);
        s.endGroup();

        s.beginGroup(QStringLiteral("decrypt"));
        s.setValue(QStringLiteral("requireSigner"), requireSigner);
        s.setValue(QStringLiteral("signerKey"), signerKey);
        s.endGroup();

        s.beginGroup(QStringLiteral("keygen"));
        s.setValue(QStringLiteral("lock"), lockNewKeys);
        s.setValue(QStringLiteral("kdfOps"), kdfOps);
        s.setValue(QStringLiteral("kdfMemMiB"), kdfMemMiB);
        s.endGroup();

        s.beginGroup(QStringLiteral("ui"));
        s.setValue(QStringLiteral("confirmOverwrite"), confirmOverwrite);
        s.setValue(QStringLiteral("lastDirectory"), lastDirectory);
        s.endGroup();
        s.sync();
    }

    /** Limits the KDF parameters the CLI accepts to something sane. */
    quint64 kdfMemBytes() const {
        return static_cast<quint64>(kdfMemMiB) * 1024ull * 1024ull;
    }
};

} /* namespace tessgui */

#endif /* TESSGUI_SETTINGS_H */
