// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DirWatcher.h"
#include "core/SelectionDb.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

using namespace picsel;

namespace {

// Longer than the watcher's settle time (1 s) so pending files would have
// been reported by then.
constexpr int kQuietMs = 1300;

bool writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

struct Spies {
    explicit Spies(DirWatcher *w)
        : added(w, &DirWatcher::fileAdded), removed(w, &DirWatcher::fileRemoved),
          renamed(w, &DirWatcher::fileRenamed), changed(w, &DirWatcher::fileChanged),
          rescan(w, &DirWatcher::rescanNeeded), gone(w, &DirWatcher::directoryGone) {}
    int total() const {
        return int(added.count() + removed.count() + renamed.count() + changed.count() + rescan.count()
                   + gone.count());
    }
    DiskFile addedFile(int i) const { return added.at(i).at(0).value<DiskFile>(); }
    DiskFile changedFile(int i) const { return changed.at(i).at(0).value<DiskFile>(); }
    QString removedName(int i) const { return removed.at(i).at(0).toString(); }
    QString renamedFrom(int i) const { return renamed.at(i).at(0).toString(); }
    DiskFile renamedTo(int i) const { return renamed.at(i).at(1).value<DiskFile>(); }

    QSignalSpy added, removed, renamed, changed, rescan, gone;
};

} // namespace

class TestWatcher : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<QTemporaryDir> m_outside; // same filesystem, for moves and links
    std::unique_ptr<DirWatcher> m_watcher;
    std::unique_ptr<Spies> m_spies;

    QString path(const QString &name) const { return QDir(m_tmp->path()).filePath(name); }
    QString outside(const QString &name) const { return QDir(m_outside->path()).filePath(name); }

    bool startWatching() {
        m_watcher = std::make_unique<DirWatcher>();
        m_spies = std::make_unique<Spies>(m_watcher.get());
        return m_watcher->start(m_tmp->path());
    }

private slots:
    void init() {
        m_tmp = std::make_unique<QTemporaryDir>();
        m_outside = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        QVERIFY(m_outside->isValid());
    }

    void cleanup() {
        m_spies.reset();
        m_watcher.reset();
        m_outside.reset();
        m_tmp.reset();
    }

    void startFailsOnMissingDir() {
        DirWatcher w;
        QVERIFY(!w.start(path("missing")));
        QVERIFY(!w.errorString().isEmpty());
        QVERIFY(writeFile(path("file.jpg"), "x"));
        QVERIFY(!w.start(path("file.jpg"))); // IN_ONLYDIR
    }

    void createWriteClose() {
        QVERIFY(startWatching());
        QFile f(path("new.jpg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("hello") == 5);
        QVERIFY(f.flush());
        QTest::qWait(300);
        QCOMPARE(m_spies->total(), 0); // not before close
        f.close();
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->added.count(), 1, 1000);
        QCOMPARE(m_spies->addedFile(0).name, QString("new.jpg"));
        QCOMPARE(m_spies->addedFile(0).size, 5);
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->added.count(), 1); // the settle timer must not report it again
        QCOMPARE(m_spies->total(), 1);
    }

    void slowWriterKeptOpen() {
        QVERIFY(startWatching());
        QFile f(path("slow.mp4"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 6; ++i) { // 1.8 s in total, longer than the settle time
            QVERIFY(f.write(QByteArray(1000, 'x')) == 1000);
            QVERIFY(f.flush());
            QTest::qWait(300);
            QCOMPARE(m_spies->total(), 0);
        }
        f.close();
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->added.count(), 1, 1000);
        QCOMPARE(m_spies->addedFile(0).name, QString("slow.mp4"));
        QCOMPARE(m_spies->addedFile(0).size, 6000);
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 1);
    }

    void hardLinkReportedAfterSettle() {
        QVERIFY(writeFile(outside("src.jpg"), "linked"));
        QVERIFY(startWatching());
        QCOMPARE(::link(QFile::encodeName(outside("src.jpg")).constData(),
                        QFile::encodeName(path("link.jpg")).constData()),
                 0);
        QTest::qWait(400);
        QCOMPARE(m_spies->total(), 0); // no CLOSE_WRITE: waits for the settle time
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->added.count(), 1, 2000);
        QCOMPARE(m_spies->addedFile(0).name, QString("link.jpg"));
        QCOMPARE(m_spies->addedFile(0).size, 6);
        QTest::qWait(300);
        QCOMPARE(m_spies->total(), 1);
    }

    void renameInsideDir() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(QFile::rename(path("a.jpg"), path("b.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->renamed.count(), 1, 1000);
        QCOMPARE(m_spies->renamedFrom(0), QString("a.jpg"));
        QCOMPARE(m_spies->renamedTo(0).name, QString("b.jpg"));
        QCOMPARE(m_spies->renamedTo(0).size, 3);
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);

        // The new name is known: deleting it is reported.
        QVERIFY(QFile::remove(path("b.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->removed.count(), 1, 1000);
        QCOMPARE(m_spies->removedName(0), QString("b.jpg"));
    }

    void renameOntoExisting() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(writeFile(path("b.jpg"), "bbbbbb"));
        QVERIFY(startWatching());
        QCOMPARE(::rename(QFile::encodeName(path("a.jpg")).constData(),
                          QFile::encodeName(path("b.jpg")).constData()),
                 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->renamed.count(), 1, 1000);
        QCOMPARE(m_spies->renamedFrom(0), QString("a.jpg"));
        QCOMPARE(m_spies->renamedTo(0).name, QString("b.jpg"));
        QCOMPARE(m_spies->renamedTo(0).size, 3);
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);
    }

    void moveOutOfDir() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(QFile::rename(path("a.jpg"), outside("a.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->removed.count(), 1, 1000);
        QCOMPARE(m_spies->removedName(0), QString("a.jpg"));
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);
    }

    void moveIntoDir() {
        QVERIFY(writeFile(outside("in.jpg"), "inside"));
        QVERIFY(startWatching());
        QVERIFY(QFile::rename(outside("in.jpg"), path("in.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->added.count(), 1, 500); // immediately, no settle wait
        QCOMPARE(m_spies->addedFile(0).name, QString("in.jpg"));
        QCOMPARE(m_spies->addedFile(0).size, 6);
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 1);
    }

    void deleteFile() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(QFile::remove(path("a.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->removed.count(), 1, 1000);
        QCOMPARE(m_spies->removedName(0), QString("a.jpg"));
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);
    }

    void deletePendingFileNotReported() {
        QVERIFY(startWatching());
        QVERIFY(writeFile(outside("src.jpg"), "x"));
        QCOMPARE(::link(QFile::encodeName(outside("src.jpg")).constData(),
                        QFile::encodeName(path("tmp.jpg")).constData()),
                 0);
        QTest::qWait(100);
        QVERIFY(QFile::remove(path("tmp.jpg")));
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 0);
    }

    void unsupportedIgnored() {
        QVERIFY(startWatching());
        QVERIFY(writeFile(path("notes.txt"), "text"));
        QVERIFY(QFile::rename(path("notes.txt"), path("notes.png")));
        QVERIFY(writeFile(path("notes.png"), "more"));
        QVERIFY(QFile::remove(path("notes.png")));
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 0);
    }

    void supportedUnsupportedRenames() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(QFile::rename(path("a.jpg"), path("a.jpg.part")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->removed.count(), 1, 1000);
        QCOMPARE(m_spies->removedName(0), QString("a.jpg"));
        QVERIFY(QFile::rename(path("a.jpg.part"), path("c.jpg")));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->added.count(), 1, 1000);
        QCOMPARE(m_spies->addedFile(0).name, QString("c.jpg"));
        QCOMPARE(m_spies->addedFile(0).size, 3);
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 2);
    }

    void atomicReplaceOfKnownFile() {
        // Editors save to a temporary name, then rename over the original.
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(writeFile(path("a.jpg.tmp"), "replaced"));
        QCOMPARE(::rename(QFile::encodeName(path("a.jpg.tmp")).constData(),
                          QFile::encodeName(path("a.jpg")).constData()),
                 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->changed.count(), 1, 1000);
        QCOMPARE(m_spies->changedFile(0).name, QString("a.jpg"));
        QCOMPARE(m_spies->changedFile(0).size, 8);
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);
    }

    void subdirectoryAndSymlinkIgnored() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(QDir(m_tmp->path()).mkdir("sub.jpg"));
        QVERIFY(writeFile(path("sub.jpg/inner.jpg"), "x"));
        QVERIFY(QDir(m_tmp->path()).rename("sub.jpg", "sub2.jpg"));
        QVERIFY(QFile::link(path("a.jpg"), path("link.jpg")));
        QVERIFY(QFile::rename(path("link.jpg"), path("link2.jpg")));
        QCOMPARE(::mkfifo(QFile::encodeName(path("fifo.jpg")).constData(), 0600), 0);
        QVERIFY(QFile::remove(path("fifo.jpg")));
        QVERIFY(QFile::remove(path("link2.jpg")));
        QVERIFY(QDir(path("sub2.jpg")).removeRecursively());
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 0);
    }

    void symlinkReplacingKnownFileIsRemoval() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(writeFile(path("b.jpg"), "bbb"));
        QVERIFY(startWatching());
        QVERIFY(QFile::link(path("b.jpg"), path("tmp-link")));
        QCOMPARE(::rename(QFile::encodeName(path("tmp-link")).constData(),
                          QFile::encodeName(path("a.jpg")).constData()),
                 0);
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->removed.count(), 1, 1000);
        QCOMPARE(m_spies->removedName(0), QString("a.jpg"));
        QTest::qWait(200);
        QCOMPARE(m_spies->total(), 1);
    }

    void databaseFilesIgnored() {
        QVERIFY(startWatching());
        {
            SelectionDb db;
            QVERIFY(db.create(m_tmp->path(), m_tmp->path(), false)); // picsel.db, -wal, -shm
            DbFile f;
            f.name = "x.jpg";
            QVERIFY(db.insertFile(f));
            QVERIFY(db.setSelected("x.jpg", true));
        }
        QVERIFY(writeFile(path("picsel.db-journal"), "j"));
        QVERIFY(QFile::remove(path("picsel.db-journal")));
        QVERIFY(QFile::rename(path("picsel.db"), path("picsel.db-old")));
        QVERIFY(QFile::rename(path("picsel.db-old"), path("picsel.db")));
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 0);
    }

    void rewriteKnownFile() {
        QVERIFY(writeFile(path("a.jpg"), "aaa"));
        QVERIFY(startWatching());
        QVERIFY(writeFile(path("a.jpg"), "rewritten"));
        QTRY_COMPARE_WITH_TIMEOUT(m_spies->changed.count(), 1, 1000);
        QCOMPARE(m_spies->changedFile(0).name, QString("a.jpg"));
        QCOMPARE(m_spies->changedFile(0).size, 9);
        QTest::qWait(kQuietMs);
        QCOMPARE(m_spies->total(), 1);
    }

    void directoryRemoved() {
        QVERIFY(QDir(m_tmp->path()).mkdir("watched"));
        QVERIFY(writeFile(path("watched/a.jpg"), "aaa"));
        DirWatcher w;
        Spies spies(&w);
        QVERIFY(w.start(path("watched")));
        QVERIFY(QDir(path("watched")).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(spies.gone.count(), 1, 1000);
        QTest::qWait(200);
        QCOMPARE(spies.gone.count(), 1);
        QVERIFY(spies.removed.count() <= 1); // a.jpg may be reported before the directory goes
        QVERIFY(spies.added.isEmpty());
    }

    void directoryMoved() {
        QVERIFY(QDir(m_tmp->path()).mkdir("watched"));
        DirWatcher w;
        Spies spies(&w);
        QVERIFY(w.start(path("watched")));
        QVERIFY(QDir(m_tmp->path()).rename("watched", "moved"));
        QTRY_COMPARE_WITH_TIMEOUT(spies.gone.count(), 1, 1000);
        // Stopped: later changes in the moved folder are not reported.
        QVERIFY(writeFile(path("moved/b.jpg"), "b"));
        QTest::qWait(200);
        QCOMPARE(spies.total(), 1);
    }

    void restartFromSlot() {
        // A slot may restart the watcher on another folder from directoryGone.
        QVERIFY(QDir(m_tmp->path()).mkdir("first"));
        QVERIFY(QDir(m_tmp->path()).mkdir("second"));
        DirWatcher w;
        Spies spies(&w);
        connect(&w, &DirWatcher::directoryGone, this, [&] { QVERIFY(w.start(path("second"))); });
        QVERIFY(w.start(path("first")));
        QVERIFY(QDir(path("first")).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(spies.gone.count(), 1, 1000);
        QVERIFY(writeFile(path("second/s.jpg"), "s"));
        QTRY_COMPARE_WITH_TIMEOUT(spies.added.count(), 1, 1000);
        QCOMPARE(spies.addedFile(0).name, QString("s.jpg"));
    }
};

QTEST_GUILESS_MAIN(TestWatcher)
#include "tst_watcher.moc"
