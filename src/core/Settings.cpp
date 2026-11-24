// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <QSettings>

#include <algorithm>

namespace picsel::settings {

namespace {
QSettings store() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("picsel"),
                     QStringLiteral("picsel"));
}

// Hand-edited or older values outside the range are clamped on the way in.
int readInt(const QString &key, IntSetting s) {
    bool ok = false;
    const int v = store().value(key).toInt(&ok);
    return ok ? s.clamp(v) : s.fallback;
}

void writeInt(const QString &key, IntSetting s, int v) {
    store().setValue(key, s.clamp(v));
}
} // namespace

int columns() { return readInt(QStringLiteral("view/columns"), kColumns); }
void setColumns(int n) { writeInt(QStringLiteral("view/columns"), kColumns, n); }

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

int similarSeconds() { return readInt(QStringLiteral("similar/seconds"), kSimilarSeconds); }
void setSimilarSeconds(int v) { writeInt(QStringLiteral("similar/seconds"), kSimilarSeconds, v); }
bool showDetails() { return store().value(QStringLiteral("view/showDetails"), false).toBool(); }
void setShowDetails(bool on) { store().setValue(QStringLiteral("view/showDetails"), on); }
bool showSimilar() { return store().value(QStringLiteral("similar/show"), true).toBool(); }
void setShowSimilar(bool on) { store().setValue(QStringLiteral("similar/show"), on); }
int similarBits() { return readInt(QStringLiteral("similar/bits"), kSimilarBits); }
void setSimilarBits(int v) { writeInt(QStringLiteral("similar/bits"), kSimilarBits, v); }

std::optional<Resume> parseResume(const QString &s) {
    if (s == QLatin1String("always"))
        return Resume::Always;
    if (s == QLatin1String("folder"))
        return Resume::Folder;
    if (s == QLatin1String("never"))
        return Resume::Never;
    return std::nullopt;
}

QString resumeToString(Resume r) {
    switch (r) {
    case Resume::Always: return QStringLiteral("always");
    case Resume::Folder: return QStringLiteral("folder");
    case Resume::Never: break;
    }
    return QStringLiteral("never");
}

Resume resume() {
    return parseResume(store().value(QStringLiteral("start/resume")).toString()).value_or(Resume::Always);
}
void setResume(Resume r) { store().setValue(QStringLiteral("start/resume"), resumeToString(r)); }

int startRow(Resume mode, int given, int remembered, bool exact) {
    int row = -1;
    switch (mode) {
    case Resume::Always:
        row = exact && remembered >= 0 ? remembered : given >= 0 ? given : remembered;
        break;
    case Resume::Folder:
        row = given >= 0 ? given : remembered;
        break;
    case Resume::Never:
        row = given;
        break;
    }
    return std::max(row, 0);
}

} // namespace picsel::settings
