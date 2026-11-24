// SPDX-License-Identifier: GPL-3.0-or-later
// DirWatcher: follows changes to a source folder with inotify.
//
// Only supported, non-database regular files are reported. The watcher keeps
// its own set of "known" names (seeded from scanDir() at start()) so that it
// can tell additions from rewrites and ignore removals/renames of files it
// never reported. Runs on the thread it lives on (normally the GUI thread).
#pragma once

#include "DirScanner.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

class QSocketNotifier;

namespace picsel {

class DirWatcher : public QObject {
    Q_OBJECT
public:
    // A new file is reported once nothing modified it for this long, even
    // without IN_CLOSE_WRITE (hard links, writers keeping the file open).
    // Public so the tests can derive their waits from it.
    static constexpr int kSettleMs = 1000;
    // How long an IN_MOVED_FROM waits for its IN_MOVED_TO in a later read.
    static constexpr int kMoveGraceMs = 80;

    explicit DirWatcher(QObject *parent = nullptr);
    ~DirWatcher() override;

    bool start(const QString &dir);   // false + errorString() on failure
    void stop();
    QString errorString() const;

signals:
    void fileAdded(const picsel::DiskFile &file);
    void fileRemoved(const QString &name);
    void fileRenamed(const QString &from, const picsel::DiskFile &to);
    void fileChanged(const picsel::DiskFile &file);   // known file rewritten in place
    void rescanNeeded();                               // IN_Q_OVERFLOW etc.: caller rescans + reconciles
    void directoryGone();                              // watched dir deleted/moved/unmounted

private:
    // A file that appeared but may still be being written.
    struct Pending {
        QDeadlineTimer deadline;
    };
    // An IN_MOVED_FROM waiting for its IN_MOVED_TO.
    struct Move {
        QString name;
        bool wasKnown = false;   // otherwise it was pending
        QDeadlineTimer deadline;
    };

    void readEvents();
    // Returns false if the watcher was stopped/restarted while handling it.
    bool handleEvent(int wd, quint32 mask, quint32 cookie, const QString &name);
    bool handleMovedTo(quint32 cookie, const QString &name);
    bool flushMovesFor(const QString &name);
    bool complete(const QString &name);
    void markPending(const QString &name);
    void onSettleTimeout();
    void onMoveTimeout();
    void scheduleSettle();
    void scheduleMoves();
    void reseed();
    std::optional<DiskFile> statOf(const QString &name) const;
    bool alive(quint64 gen) const { return gen == m_generation; }

    QString m_dir;
    QString m_error;
    int m_fd = -1;
    int m_wd = -1;
    QSocketNotifier *m_notifier = nullptr;
    quint64 m_generation = 0;

    QSet<QString> m_known;
    QHash<QString, Pending> m_pending;
    QHash<quint32, Move> m_moves;
    QTimer m_settleTimer;
    QTimer m_moveTimer;
};

} // namespace picsel

Q_DECLARE_METATYPE(picsel::DiskFile)
