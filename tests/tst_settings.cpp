// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Settings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace picsel;

class TestSettings : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    // The file settings.cpp reads, opened directly to plant raw values.
    static QSettings raw() {
        return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("picsel"),
                         QStringLiteral("picsel"));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        // Never touch the user's real settings.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init() { raw().clear(); }

    void defaultsWhenUnset() {
        QCOMPARE(settings::columns(), settings::kColumns.fallback);
        QCOMPARE(settings::similarSeconds(), settings::kSimilarSeconds.fallback);
        QCOMPARE(settings::similarBits(), settings::kSimilarBits.fallback);
    }

    void roundTripInsideRange() {
        settings::setColumns(settings::kColumns.max);
        QCOMPARE(settings::columns(), settings::kColumns.max);
        settings::setSimilarSeconds(settings::kSimilarSeconds.min);
        QCOMPARE(settings::similarSeconds(), settings::kSimilarSeconds.min);
    }

    void writesAreClamped() {
        settings::setColumns(settings::kColumns.max + 5);
        QCOMPARE(raw().value("view/columns").toInt(), settings::kColumns.max);
        settings::setSimilarBits(-3);
        QCOMPARE(raw().value("similar/bits").toInt(), settings::kSimilarBits.min);
    }

    void storedValuesOutsideTheRangeAreClamped() {
        // Hand-edited, or written by an older picsel with wider limits.
        raw().setValue("view/columns", 99);
        raw().setValue("similar/seconds", 3600);
        raw().setValue("similar/bits", 0);
        QCOMPARE(settings::columns(), settings::kColumns.max);
        QCOMPARE(settings::similarSeconds(), settings::kSimilarSeconds.max);
        QCOMPARE(settings::similarBits(), settings::kSimilarBits.min);
    }

    void garbageFallsBackToTheDefault() {
        raw().setValue("view/columns", "many");
        QCOMPARE(settings::columns(), settings::kColumns.fallback);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
