// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sorting.h"

#include <QDateTime>
#include <QTimeZone>

namespace picsel {

SortMode sortModeFromString(const QString &s) {
    return s == QLatin1String("date") ? SortMode::Date : SortMode::Name;
}

QString sortModeToString(SortMode m) {
    return m == SortMode::Date ? QStringLiteral("date") : QStringLiteral("name");
}

qint64 correctedTimeMs(const QString &exifTime, const QString &camera, const CameraOffsets &offsets) {
    if (exifTime.isEmpty())
        return kNoTime;
    QDateTime dt = QDateTime::fromString(exifTime, Qt::ISODateWithMs);
    if (!dt.isValid())
        return kNoTime;
    // Treat camera time as UTC purely as a naive scale (no DST surprises).
    dt = QDateTime(dt.date(), dt.time(), QTimeZone::UTC);
    return dt.toMSecsSinceEpoch() + offsets.value(camera, 0) * 1000;
}

int compareNames(const QString &a, const QString &b) {
    // Natural order: digit runs compare by value, everything else
    // case-insensitively. Locale-independent, so it behaves the same under
    // any LANG (QCollator ignores numeric mode in the C locale).
    qsizetype i = 0, j = 0;
    const qsizetype na = a.size(), nb = b.size();
    while (i < na && j < nb) {
        const QChar ca = a[i], cb = b[j];
        if (ca.isDigit() && cb.isDigit()) {
            qsizetype si = i, sj = j;
            while (si < na && a[si] == QLatin1Char('0'))
                ++si;
            while (sj < nb && b[sj] == QLatin1Char('0'))
                ++sj;
            qsizetype ei = si, ej = sj;
            while (ei < na && a[ei].isDigit())
                ++ei;
            while (ej < nb && b[ej].isDigit())
                ++ej;
            if (ei - si != ej - sj)
                return ei - si < ej - sj ? -1 : 1;
            for (qsizetype k = 0; k < ei - si; ++k)
                if (a[si + k] != b[sj + k])
                    return a[si + k] < b[sj + k] ? -1 : 1;
            if ((si - i) != (sj - j)) // same value: fewer leading zeros first
                return (si - i) < (sj - j) ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        const QChar la = ca.toCaseFolded(), lb = cb.toCaseFolded();
        if (la != lb)
            return la < lb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < na || j < nb)
        return i < na ? 1 : -1;
    return a.compare(b); // total order even for case-only differences
}

bool sortLess(const SortKey &a, const SortKey &b, SortMode mode) {
    if (mode == SortMode::Date && a.timeMs != b.timeMs)
        return a.timeMs < b.timeMs; // kNoTime is max, so undated go last
    return compareNames(a.name, b.name) < 0;
}

} // namespace picsel
