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

    void resumeDefaultsToAlwaysAndRoundTrips() {
        QCOMPARE(settings::resume(), settings::Resume::Always);
        for (auto r : {settings::Resume::Folder, settings::Resume::Never, settings::Resume::Always}) {
            settings::setResume(r);
            QCOMPARE(settings::resume(), r);
            QCOMPARE(settings::parseResume(settings::resumeToString(r)), std::optional(r));
        }
        QCOMPARE(raw().value("start/resume").toString(), QStringLiteral("always"));
    }

    void unknownResumeIsRejected() {
        QCOMPARE(settings::parseResume(QStringLiteral("Always")), std::nullopt);
        QCOMPARE(settings::parseResume(QString()), std::nullopt);
        raw().setValue("start/resume", "sometimes");
        QCOMPARE(settings::resume(), settings::Resume::Always);
    }

    void startRow_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("given");      // -1: a folder was opened
        QTest::addColumn<int>("remembered"); // -1: nothing remembered
        QTest::addColumn<bool>("exact");     // false: the remembered picture is gone
        QTest::addColumn<bool>("newByDate"); // a folder just set up, sorted by date
        QTest::addColumn<int>("expected");
        const int always = int(settings::Resume::Always), folder = int(settings::Resume::Folder),
                  never = int(settings::Resume::Never);
        QTest::newRow("always, folder") << always << -1 << 7 << true << false << 7;
        QTest::newRow("always, picture") << always << 3 << 7 << true << false << 7;
        QTest::newRow("always, picture, remembered gone") << always << 3 << 7 << false << false << 3;
        QTest::newRow("always, folder, remembered gone") << always << -1 << 7 << false << false << 7;
        QTest::newRow("always, picture, nothing remembered") << always << 3 << -1 << false << false << 3;
        QTest::newRow("folder mode, folder") << folder << -1 << 7 << true << false << 7;
        QTest::newRow("folder mode, picture") << folder << 3 << 7 << true << false << 3;
        QTest::newRow("never, folder") << never << -1 << 7 << true << false << 0;
        QTest::newRow("never, picture") << never << 3 << 7 << true << false << 3;
        QTest::newRow("nothing at all") << always << -1 << -1 << false << false << 0;
        QTest::newRow("always, new folder by date") << always << 3 << -1 << false << true << 0;
        QTest::newRow("folder mode, new folder by date") << folder << 3 << -1 << false << true << 3;
        QTest::newRow("never, new folder by date") << never << 3 << -1 << false << true << 3;
    }

    void startRow() {
        QFETCH(int, mode);
        QFETCH(int, given);
        QFETCH(int, remembered);
        QFETCH(bool, exact);
        QFETCH(bool, newByDate);
        QFETCH(int, expected);
        QCOMPARE(settings::startRow(settings::Resume(mode), given, remembered, exact, newByDate), expected);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
