#ifndef TESSGUI_SETTINGS_H
#define TESSGUI_SETTINGS_H

#include <QSettings>
#include <QString>

namespace tessgui {

struct GuiSettings {
    bool armor = false;
    quint32 chunkSize = 0;
    bool signWhenPossible = true;
    QString senderKey;

    bool requireSigner = false;
    QString signerKey;

    bool lockNewKeys = false;
    quint32 kdfOps = 0;
    quint32 kdfMemMiB = 0;

    bool confirmOverwrite = true;
    QString lastDirectory;

    bool usePe = false;
    quint32 peChunkMiB = 256;
    bool includeSubdirs = false;
    QString batchInputDir;
    QString batchOutputDir;

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
        s.beginGroup(QStringLiteral("pe"));
        g.usePe = s.value(QStringLiteral("usePe"), g.usePe).toBool();
        g.peChunkMiB = s.value(QStringLiteral("peChunkMiB"), g.peChunkMiB).toUInt();
        g.includeSubdirs = s.value(QStringLiteral("includeSubdirs"), g.includeSubdirs).toBool();
        g.batchInputDir = s.value(QStringLiteral("batchInputDir")).toString();
        g.batchOutputDir = s.value(QStringLiteral("batchOutputDir")).toString();
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
        s.beginGroup(QStringLiteral("pe"));
        s.setValue(QStringLiteral("usePe"), usePe);
        s.setValue(QStringLiteral("peChunkMiB"), peChunkMiB);
        s.setValue(QStringLiteral("includeSubdirs"), includeSubdirs);
        s.setValue(QStringLiteral("batchInputDir"), batchInputDir);
        s.setValue(QStringLiteral("batchOutputDir"), batchOutputDir);
        s.endGroup();
        s.sync();
    }

    quint64 kdfMemBytes() const {
        return static_cast<quint64>(kdfMemMiB) * 1024ull * 1024ull;
    }
};

}
#endif