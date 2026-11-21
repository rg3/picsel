#include "Settings.h"

#include <QSettings>

namespace picsel::settings {

namespace {
QSettings store() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("picsel"),
                     QStringLiteral("picsel"));
}
} // namespace

int columns() {
    return std::clamp(store().value(QStringLiteral("view/columns"), 4).toInt(), 2, 6);
}

void setColumns(int n) {
    store().setValue(QStringLiteral("view/columns"), std::clamp(n, 2, 6));
}

CameraOffsets cameraOffsets() {
    CameraOffsets out;
    QSettings s = store();
    const int n = s.beginReadArray(QStringLiteral("cameraOffsets"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        out.insert(s.value(QStringLiteral("camera")).toString(),
                   s.value(QStringLiteral("seconds")).toLongLong());
    }
    s.endArray();
    return out;
}

void setCameraOffset(const QString &camera, qint64 seconds) {
    CameraOffsets all = cameraOffsets();
    if (seconds == 0)
        all.remove(camera);
    else
        all.insert(camera, seconds);
    QSettings s = store();
    s.remove(QStringLiteral("cameraOffsets"));
    s.beginWriteArray(QStringLiteral("cameraOffsets"), int(all.size()));
    int i = 0;
    for (auto it = all.cbegin(); it != all.cend(); ++it, ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("camera"), it.key());
        s.setValue(QStringLiteral("seconds"), it.value());
    }
    s.endArray();
}

int similarSeconds() { return store().value(QStringLiteral("similar/seconds"), 10).toInt(); }
void setSimilarSeconds(int v) { store().setValue(QStringLiteral("similar/seconds"), v); }
int similarBits() { return store().value(QStringLiteral("similar/bits"), 10).toInt(); }
void setSimilarBits(int v) { store().setValue(QStringLiteral("similar/bits"), v); }

} // namespace picsel::settings
