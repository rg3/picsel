// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Mover.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTest>

#include <atomic>
#include <functional>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace picsel;

namespace {

bool writeFile(const QString &dir, const QString &name, const QByteArray &data) {
    QFile f(dir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString &dir, const QString &name) {
    QFile f(dir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool exists(const QString &dir, const QString &name) {
    struct stat st;
    return ::lstat(QFile::encodeName(dir + QLatin1Char('/') + name).constData(), &st) == 0;
}

struct stat statOf(const QString &dir, const QString &name) {
    struct stat st {};
    ::lstat(QFile::encodeName(dir + QLatin1Char('/') + name).constData(), &st);
    return st;
}

QStringList tempLeftovers(const QString &dir) {
    return QDir(dir).entryList({QLatin1String(kMoveTempPrefix) + QLatin1Char('*')},
                               QDir::AllEntries | QDir::Hidden | QDir::System);
}

// Source and destination folders inside one temp dir (same filesystem).
struct SameFs {
    QTemporaryDir root;
    QString src, dst;
    SameFs() {
        QDir(root.path()).mkdir(QStringLiteral("src"));
        QDir(root.path()).mkdir(QStringLiteral("dst"));
        src = root.path() + QStringLiteral("/src");
        dst = root.path() + QStringLiteral("/dst");
    }
};

// Runs a job to completion. |onConflict| answers conflicts (from the worker
// thread). Returns the finished() arguments.
struct RunResult {
    QVector<MoveOutcome> outcomes;
    bool cancelled = false;
    int conflicts = 0;
    QList<QList<QVariant>> progress;
    bool ok = false;
};

RunResult runJob(MoveJob &job, std::function<void(MoveJob &, const QString &)> onConflict = {}) {
    RunResult r;
    auto conflicts = std::make_shared<std::atomic_int>(0);
    QObject::connect(&job, &MoveJob::conflictFound, &job, [&job, onConflict, conflicts](const QString &name) {
        ++*conflicts;
        if (onConflict)
            onConflict(job, name);
    }, Qt::DirectConnection);
    QSignalSpy progress(&job, &MoveJob::progress);
    QSignalSpy finished(&job, &MoveJob::finished);
    job.start();
    r.ok = finished.wait(20000);
    if (!r.ok)
        return r;
    r.outcomes = finished.at(0).at(0).value<QVector<MoveOutcome>>();
    r.cancelled = finished.at(0).at(1).toBool();
    r.conflicts = *conflicts;
    r.progress = progress;
    return r;
}

auto answer(ConflictAction a, bool all = false) {
    return [a, all](MoveJob &job, const QString &) { job.resolveConflict(a, all); };
}

} // namespace

class TestMover : public QObject {
    Q_OBJECT
private slots:
    // ---- preflight ----
    void preflightMissingTarget() {
        SameFs fs;
        const auto r = preflightMove(fs.src, fs.root.path() + QStringLiteral("/nope"), {"a.jpg"});
        QVERIFY(!r.ok());
        QVERIFY(!r.fatal.isEmpty());
    }

    void preflightTargetIsFile() {
        SameFs fs;
        QVERIFY(writeFile(fs.root.path(), "file", "x"));
        const auto r = preflightMove(fs.src, fs.root.path() + QStringLiteral("/file"), {"a.jpg"});
        QVERIFY(!r.fatal.isEmpty());
    }

    void preflightTargetIsSource() {
        SameFs fs;
        QVERIFY(!preflightMove(fs.src, fs.src, {"a.jpg"}).fatal.isEmpty());
        QVERIFY(!preflightMove(fs.src, fs.dst + QStringLiteral("/../src/"), {"a.jpg"}).fatal.isEmpty());
    }

    void preflightSourceUnreadable() {
        SameFs fs;
        QVERIFY(!preflightMove(fs.root.path() + QStringLiteral("/nope"), fs.dst, {"a.jpg"}).fatal.isEmpty());
    }

    void preflightTargetNotWritable() {
        if (::geteuid() == 0)
            QSKIP("root can write anywhere");
        SameFs fs;
        QVERIFY(::chmod(QFile::encodeName(fs.dst).constData(), 0555) == 0);
        const auto r = preflightMove(fs.src, fs.dst, {"a.jpg"});
        ::chmod(QFile::encodeName(fs.dst).constData(), 0755);
        QVERIFY(!r.fatal.isEmpty());
    }

    void preflightBlockedAndConflicts() {
        SameFs fs;
        for (const char *n : {"a.jpg", "b.jpg", "c.jpg", "d.jpg"})
            QVERIFY(writeFile(fs.src, n, "src"));
        QVERIFY(QDir(fs.dst).mkdir("a.jpg"));
        QVERIFY(::symlink("/etc/hostname", QFile::encodeName(fs.dst + "/b.jpg").constData()) == 0);
        QVERIFY(writeFile(fs.dst, "c.jpg", "old"));
        const auto r = preflightMove(fs.src, fs.dst, {"a.jpg", "b.jpg", "c.jpg", "d.jpg"});
        QVERIFY(r.fatal.isEmpty());
        QVERIFY(!r.ok());
        QCOMPARE(r.blocked, (QStringList{"a.jpg", "b.jpg"}));
        QCOMPARE(r.conflicts, (QStringList{"c.jpg"}));

        const auto ok = preflightMove(fs.src, fs.dst, {"c.jpg", "d.jpg"});
        QVERIFY(ok.ok());
        QCOMPARE(ok.conflicts, (QStringList{"c.jpg"}));
    }

    void preflightDanglingSymlinkBlocks() {
        SameFs fs;
        QVERIFY(::symlink("/nonexistent", QFile::encodeName(fs.dst + "/a.jpg").constData()) == 0);
        QCOMPARE(preflightMove(fs.src, fs.dst, {"a.jpg"}).blocked, (QStringList{"a.jpg"}));
    }

    // ---- suffixedName ----
    void suffixed() {
        QTemporaryDir d;
        QCOMPARE(suffixedName(d.path(), "a.jpg"), QStringLiteral("a-1.jpg"));
        QVERIFY(writeFile(d.path(), "a.jpg", "x"));
        QVERIFY(writeFile(d.path(), "a-1.jpg", "x"));
        QCOMPARE(suffixedName(d.path(), "a.jpg"), QStringLiteral("a-2.jpg"));
        QCOMPARE(suffixedName(d.path(), "IMG_1.JPG"), QStringLiteral("IMG_1-1.JPG"));
        QCOMPARE(suffixedName(d.path(), "name"), QStringLiteral("name-1"));
        QCOMPARE(suffixedName(d.path(), ".x"), QStringLiteral(".x-1"));
        QCOMPARE(suffixedName(d.path(), "a.b.jpg"), QStringLiteral("a.b-1.jpg"));
        // Any existing entry counts as taken, including dangling symlinks.
        QVERIFY(::symlink("/nonexistent", QFile::encodeName(d.path() + "/name-1").constData()) == 0);
        QCOMPARE(suffixedName(d.path(), "name"), QStringLiteral("name-2"));
    }

    // ---- same filesystem ----
    void sameFsMove() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "hello"));
        QVERIFY(writeFile(fs.src, "b.jpg", "world"));
        const ino_t ino = statOf(fs.src, "a.jpg").st_ino;
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg"});
        QSignalSpy moved(&job, &MoveJob::fileMoved);
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        QVERIFY(!r.cancelled);
        QCOMPARE(r.outcomes.size(), 2);
        for (const auto &o : r.outcomes) {
            QCOMPARE(o.status, MoveOutcome::Moved);
            QCOMPARE(o.destName, o.name);
        }
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("hello"));
        QCOMPARE(readFile(fs.dst, "b.jpg"), QByteArray("world"));
        QCOMPARE(statOf(fs.dst, "a.jpg").st_ino, ino);
        QVERIFY(!exists(fs.src, "a.jpg"));
        QVERIFY(!exists(fs.src, "b.jpg"));
        QCOMPARE(r.conflicts, 0);
        QCOMPARE(moved.size(), 2);
        QCOMPARE(moved.at(1).at(0).toString(), QStringLiteral("b.jpg"));
    }

    void progressCounts() {
        SameFs fs;
        for (const char *n : {"a.jpg", "b.jpg", "c.jpg"})
            QVERIFY(writeFile(fs.src, n, n));
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg", "c.jpg"});
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        QCOMPARE(r.progress.size(), 4);
        const QStringList names{"a.jpg", "b.jpg", "c.jpg", ""};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(r.progress.at(i).at(0).toInt(), i);
            QCOMPARE(r.progress.at(i).at(1).toInt(), 3);
            QCOMPARE(r.progress.at(i).at(2).toString(), names.at(i));
        }
    }

    void missingSourceFails() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "b.jpg", "b"));
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg"});
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Failed);
        QVERIFY(!r.outcomes.at(0).error.isEmpty());
        QCOMPARE(r.outcomes.at(1).status, MoveOutcome::Moved);
    }

    void conflictOverwrite() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        MoveJob job(fs.src, fs.dst, {"a.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Overwrite));
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts, 1);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(r.outcomes.at(0).destName, QStringLiteral("a.jpg"));
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("new"));
        QVERIFY(!exists(fs.src, "a.jpg"));
    }

    void overwriteHardLinkOfSource() {
        // rename() onto another link of the same inode is a no-op: the
        // source name must still go away.
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "same"));
        QCOMPARE(::link(QFile::encodeName(fs.src + "/a.jpg").constData(),
                        QFile::encodeName(fs.dst + "/a.jpg").constData()), 0);
        MoveJob job(fs.src, fs.dst, {"a.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Overwrite));
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts, 1);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(r.outcomes.at(0).destName, QStringLiteral("a.jpg"));
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("same"));
        QCOMPARE(statOf(fs.dst, "a.jpg").st_nlink, nlink_t(1));
        QVERIFY(!exists(fs.src, "a.jpg"));
    }

    void conflictRename() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.JPG", "new"));
        QVERIFY(writeFile(fs.dst, "a.JPG", "old"));
        QVERIFY(writeFile(fs.dst, "a-1.JPG", "old1"));
        MoveJob job(fs.src, fs.dst, {"a.JPG"});
        const RunResult r = runJob(job, answer(ConflictAction::Rename));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(r.outcomes.at(0).destName, QStringLiteral("a-2.JPG"));
        QCOMPARE(readFile(fs.dst, "a.JPG"), QByteArray("old"));
        QCOMPARE(readFile(fs.dst, "a-1.JPG"), QByteArray("old1"));
        QCOMPARE(readFile(fs.dst, "a-2.JPG"), QByteArray("new"));
        QVERIFY(!exists(fs.src, "a.JPG"));
    }

    void conflictSkip() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.src, "b.jpg", "b"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Skip));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Skipped);
        QCOMPARE(r.outcomes.at(1).status, MoveOutcome::Moved);
        QCOMPARE(readFile(fs.src, "a.jpg"), QByteArray("new"));
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("old"));
    }

    void conflictApplyToAll() {
        SameFs fs;
        for (const char *n : {"a.jpg", "b.jpg", "c.jpg"}) {
            QVERIFY(writeFile(fs.src, n, "new"));
            QVERIFY(writeFile(fs.dst, n, "old"));
        }
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg", "c.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Overwrite, true));
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts, 1);
        for (const auto &o : r.outcomes)
            QCOMPARE(o.status, MoveOutcome::Moved);
        for (const char *n : {"a.jpg", "b.jpg", "c.jpg"})
            QCOMPARE(readFile(fs.dst, n), QByteArray("new"));
    }

    void conflictAnsweredFromGuiThread() {
        // Queued conflictFound to a receiver in this thread, answered later.
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        MoveJob job(fs.src, fs.dst, {"a.jpg"});
        QObject receiver;
        QStringList asked;
        connect(&job, &MoveJob::conflictFound, &receiver, [&](const QString &name) {
            QCOMPARE(QThread::currentThread(), receiver.thread());
            asked << name;
            QTimer::singleShot(50, &receiver, [&job] { job.resolveConflict(ConflictAction::Rename, false); });
        });
        QSignalSpy finished(&job, &MoveJob::finished);
        job.start();
        QVERIFY(finished.wait(20000));
        QCOMPARE(asked, (QStringList{"a.jpg"}));
        const auto outcomes = finished.at(0).at(0).value<QVector<MoveOutcome>>();
        QCOMPARE(outcomes.at(0).destName, QStringLiteral("a-1.jpg"));
    }

    void overwriteRefusesNonRegular() {
        // Applied to all, but the second target is a directory: never overwritten.
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.src, "b.jpg", "new"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        QVERIFY(QDir(fs.dst).mkdir("b.jpg"));
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Overwrite, true));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(r.outcomes.at(1).status, MoveOutcome::Failed);
        QVERIFY(exists(fs.src, "b.jpg"));
    }

    void targetAppearsAfterPreflight() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "a"));
        QVERIFY(writeFile(fs.src, "b.jpg", "b"));
        QVERIFY(writeFile(fs.src, "c.jpg", "c"));
        QVERIFY(preflightMove(fs.src, fs.dst, {"a.jpg", "b.jpg", "c.jpg"}).ok());
        QVERIFY(QDir(fs.dst).mkdir("a.jpg"));
        QVERIFY(::symlink("/etc/hostname", QFile::encodeName(fs.dst + "/b.jpg").constData()) == 0);
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg", "c.jpg"});
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts, 0);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Failed);
        QVERIFY(r.outcomes.at(0).error.contains("not a regular file"));
        QCOMPARE(r.outcomes.at(1).status, MoveOutcome::Failed);
        QCOMPARE(r.outcomes.at(2).status, MoveOutcome::Moved);
        QVERIFY(exists(fs.src, "a.jpg"));
        QVERIFY(exists(fs.src, "b.jpg"));
        QVERIFY(QFileInfo(fs.dst + "/b.jpg").isSymLink());
    }

    void cancelMidway() {
        SameFs fs;
        const QStringList names{"a.jpg", "b.jpg", "c.jpg", "d.jpg"};
        for (const QString &n : names)
            QVERIFY(writeFile(fs.src, n, n.toUtf8()));
        MoveJob job(fs.src, fs.dst, names);
        connect(&job, &MoveJob::fileMoved, &job, [&job](const QString &name) {
            if (name == QLatin1String("b.jpg"))
                job.cancel();
        }, Qt::DirectConnection);
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        QVERIFY(r.cancelled);
        QCOMPARE(r.outcomes.size(), 2);
        QCOMPARE(r.progress.last().at(0).toInt(), 2);
        QVERIFY(exists(fs.dst, "b.jpg"));
        QVERIFY(exists(fs.src, "c.jpg"));
        QVERIFY(exists(fs.src, "d.jpg"));
    }

    void cancelPendingConflict() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.src, "b.jpg", "b"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        MoveJob job(fs.src, fs.dst, {"a.jpg", "b.jpg"});
        const RunResult r = runJob(job, [](MoveJob &j, const QString &) { j.cancel(); });
        QVERIFY(r.ok);
        QVERIFY(r.cancelled);
        QCOMPARE(r.outcomes.size(), 1);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Skipped);
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("old"));
        QVERIFY(exists(fs.src, "a.jpg"));
        QVERIFY(exists(fs.src, "b.jpg"));
    }

    void destroyWhileWaitingForConflict() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "new"));
        QVERIFY(writeFile(fs.dst, "a.jpg", "old"));
        {
            MoveJob job(fs.src, fs.dst, {"a.jpg"});
            QSignalSpy conflict(&job, &MoveJob::conflictFound);
            job.start();
            QVERIFY(conflict.wait(20000));
        } // destructor cancels and joins
        QCOMPARE(readFile(fs.src, "a.jpg"), QByteArray("new"));
        QCOMPARE(readFile(fs.dst, "a.jpg"), QByteArray("old"));
    }

    void jobRefusesSameFolder() {
        SameFs fs;
        QVERIFY(writeFile(fs.src, "a.jpg", "a"));
        MoveJob job(fs.src, fs.src, {"a.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Rename));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Failed);
        QCOMPARE(QDir(fs.src).entryList(QDir::Files), (QStringList{"a.jpg"}));
    }

    // ---- cross filesystem ----
    void crossFsMove() {
        CrossFs fs;
        if (!fs.ready())
            QSKIP(qPrintable(fs.skipReason));
        QByteArray big(3 * 1024 * 1024 + 17, Qt::Uninitialized);
        for (qsizetype i = 0; i < big.size(); ++i)
            big[i] = char((i * 131) ^ (i >> 7));
        QVERIFY(writeFile(fs.src(), "big.jpg", big));
        QVERIFY(writeFile(fs.src(), "small.jpg", "small"));
        QVERIFY(writeFile(fs.src(), "empty.jpg", ""));
        const QByteArray p = QFile::encodeName(fs.src() + "/big.jpg");
        QVERIFY(::chmod(p.constData(), 0640) == 0);
        const struct timespec times[2] = {{1600000000, 123456789}, {1700000000, 987654321}};
        QVERIFY(::utimensat(AT_FDCWD, p.constData(), times, AT_SYMLINK_NOFOLLOW) == 0);

        MoveJob job(fs.src(), fs.dst(), {"big.jpg", "small.jpg", "empty.jpg"});
        const RunResult r = runJob(job);
        QVERIFY(r.ok);
        for (const auto &o : r.outcomes)
            QVERIFY2(o.status == MoveOutcome::Moved, qPrintable(o.error));
        QCOMPARE(readFile(fs.dst(), "big.jpg"), big);
        QCOMPARE(readFile(fs.dst(), "small.jpg"), QByteArray("small"));
        QVERIFY(exists(fs.dst(), "empty.jpg"));
        QCOMPARE(QFileInfo(fs.dst() + "/empty.jpg").size(), 0);
        const struct stat st = statOf(fs.dst(), "big.jpg");
        QCOMPARE(st.st_mode & 07777, mode_t(0640));
        QCOMPARE(st.st_mtim.tv_sec, time_t(1700000000));
        QCOMPARE(st.st_mtim.tv_nsec, 987654321L);
        QVERIFY(!exists(fs.src(), "big.jpg"));
        QVERIFY(!exists(fs.src(), "small.jpg"));
        QVERIFY(!exists(fs.src(), "empty.jpg"));
        QCOMPARE(tempLeftovers(fs.dst()), QStringList());
    }

    void crossFsOverwrite() {
        CrossFs fs;
        if (!fs.ready())
            QSKIP(qPrintable(fs.skipReason));
        QVERIFY(writeFile(fs.src(), "a.jpg", "new"));
        QVERIFY(writeFile(fs.dst(), "a.jpg", "old content"));
        MoveJob job(fs.src(), fs.dst(), {"a.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Overwrite));
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts, 1);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(readFile(fs.dst(), "a.jpg"), QByteArray("new"));
        QVERIFY(!exists(fs.src(), "a.jpg"));
        QCOMPARE(tempLeftovers(fs.dst()), QStringList());
    }

    void crossFsRename() {
        CrossFs fs;
        if (!fs.ready())
            QSKIP(qPrintable(fs.skipReason));
        QVERIFY(writeFile(fs.src(), "a.jpg", "new"));
        QVERIFY(writeFile(fs.dst(), "a.jpg", "old"));
        MoveJob job(fs.src(), fs.dst(), {"a.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Rename));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Moved);
        QCOMPARE(r.outcomes.at(0).destName, QStringLiteral("a-1.jpg"));
        QCOMPARE(readFile(fs.dst(), "a.jpg"), QByteArray("old"));
        QCOMPARE(readFile(fs.dst(), "a-1.jpg"), QByteArray("new"));
        QVERIFY(!exists(fs.src(), "a.jpg"));
        QCOMPARE(tempLeftovers(fs.dst()), QStringList());
    }

    void crossFsSkipAndBlocked() {
        CrossFs fs;
        if (!fs.ready())
            QSKIP(qPrintable(fs.skipReason));
        QVERIFY(writeFile(fs.src(), "a.jpg", "new"));
        QVERIFY(writeFile(fs.src(), "b.jpg", "b"));
        QVERIFY(writeFile(fs.dst(), "a.jpg", "old"));
        QVERIFY(QDir(fs.dst()).mkdir("b.jpg"));
        MoveJob job(fs.src(), fs.dst(), {"a.jpg", "b.jpg"});
        const RunResult r = runJob(job, answer(ConflictAction::Skip));
        QVERIFY(r.ok);
        QCOMPARE(r.outcomes.at(0).status, MoveOutcome::Skipped);
        QCOMPARE(r.outcomes.at(1).status, MoveOutcome::Failed);
        QVERIFY(exists(fs.src(), "a.jpg"));
        QVERIFY(exists(fs.src(), "b.jpg"));
        QCOMPARE(readFile(fs.dst(), "a.jpg"), QByteArray("old"));
        QCOMPARE(tempLeftovers(fs.dst()), QStringList());
    }

private:
    // Source on tmpfs (/dev/shm), destination under ~/.cache.
    struct CrossFs {
        std::unique_ptr<QTemporaryDir> s, d;
        QString skipReason;
        CrossFs() {
            if (!QFileInfo(QStringLiteral("/dev/shm")).isDir()) {
                skipReason = QStringLiteral("/dev/shm not available");
                return;
            }
            const QString cache = QDir::homePath() + QStringLiteral("/.cache");
            QDir().mkpath(cache);
            s = std::make_unique<QTemporaryDir>(QStringLiteral("/dev/shm/tst_mover-XXXXXX"));
            d = std::make_unique<QTemporaryDir>(cache + QStringLiteral("/tst_mover-XXXXXX"));
            if (!s->isValid() || !d->isValid()) {
                skipReason = QStringLiteral("cannot create the temp dirs");
                return;
            }
            struct stat a, b;
            if (::stat(QFile::encodeName(s->path()).constData(), &a) != 0
                || ::stat(QFile::encodeName(d->path()).constData(), &b) != 0 || a.st_dev == b.st_dev)
                skipReason = QStringLiteral("/dev/shm and ~/.cache are on the same filesystem");
        }
        bool ready() const { return skipReason.isEmpty(); }
        QString src() const { return s->path(); }
        QString dst() const { return d->path(); }
    };
};

QTEST_GUILESS_MAIN(TestMover)
#include "tst_mover.moc"
