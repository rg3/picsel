// SPDX-License-Identifier: GPL-3.0-or-later
#include "DirWatcher.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <optional>
#include <sys/inotify.h>
#include <unistd.h>

namespace picsel {

namespace {

// A new file is reported once nothing modified it for this long, even without
// IN_CLOSE_WRITE (hard links, writers keeping the file open).
constexpr int kSettleMs = 1000;
// How long an IN_MOVED_FROM waits for its IN_MOVED_TO in a later read.
constexpr int kMoveGraceMs = 80;

constexpr quint32 kWatchMask = IN_CREATE | IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO
    | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_EXCL_UNLINK;

bool isRelevantName(const QString &name) {
    return !name.isEmpty() && isSupportedName(name) && !isDatabaseFile(name);
}

} // namespace

DirWatcher::DirWatcher(QObject *parent) : QObject(parent) {
    qRegisterMetaType<picsel::DiskFile>();
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setTimerType(Qt::PreciseTimer);
    m_moveTimer.setSingleShot(true);
    m_moveTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_settleTimer, &QTimer::timeout, this, &DirWatcher::onSettleTimeout);
    connect(&m_moveTimer, &QTimer::timeout, this, &DirWatcher::onMoveTimeout);
}

DirWatcher::~DirWatcher() { stop(); }

QString DirWatcher::errorString() const { return m_error; }

bool DirWatcher::start(const QString &dir) {
    stop();
    m_error.clear();
    const QString abs = QDir(dir).absolutePath();

    m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        m_error = QStringLiteral("inotify_init1: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    // Watch before scanning so nothing created in between is missed.
    m_wd = ::inotify_add_watch(m_fd, QFile::encodeName(abs).constData(), kWatchMask);
    if (m_wd < 0) {
        const int err = errno;
        m_error = QStringLiteral("cannot watch %1: %2").arg(abs, QString::fromLocal8Bit(std::strerror(err)));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_dir = abs;
    reseed();
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &DirWatcher::readEvents);
    return true;
}

void DirWatcher::stop() {
    ++m_generation;
    m_settleTimer.stop();
    m_moveTimer.stop();
    if (m_notifier) {
        // May be called from within the notifier's own activation.
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd); // also drops the watch
        m_fd = -1;
    }
    m_wd = -1;
    m_dir.clear();
    m_known.clear();
    m_pending.clear();
    m_moves.clear();
}

void DirWatcher::reseed() {
    m_known.clear();
    m_pending.clear();
    m_moves.clear();
    m_settleTimer.stop();
    m_moveTimer.stop();
    for (const DiskFile &f : scanDir(m_dir))
        m_known.insert(f.name);
}

std::optional<DiskFile> DirWatcher::statOf(const QString &name) const {
    return statEntry(m_dir, name);
}

void DirWatcher::readEvents() {
    const quint64 gen = m_generation;
    alignas(struct inotify_event) char buf[16 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    while (alive(gen) && m_fd >= 0) {
        const ssize_t n = ::read(m_fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break; // EAGAIN: queue drained
        }
        if (n == 0)
            break;
        for (const char *p = buf; p < buf + n;) {
            const auto *ev = reinterpret_cast<const struct inotify_event *>(p);
            p += sizeof(struct inotify_event) + ev->len;
            const QString name = ev->len ? QFile::decodeName(ev->name) : QString();
            if (!handleEvent(ev->wd, ev->mask, ev->cookie, name))
                return;
        }
    }
    if (alive(gen)) {
        scheduleSettle();
        scheduleMoves();
    }
}

bool DirWatcher::handleEvent(int wd, quint32 mask, quint32 cookie, const QString &name) {
    const quint64 gen = m_generation;
    if (mask & IN_Q_OVERFLOW) {
        reseed();
        emit rescanNeeded();
        return alive(gen);
    }
    if (wd != m_wd)
        return true;
    if (mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED | IN_UNMOUNT)) {
        // Stop first so a slot may safely start() watching something else.
        stop();
        emit directoryGone();
        return false;
    }
    if (name.isEmpty() || (mask & IN_ISDIR))
        return true;
    if (mask & IN_MOVED_TO)
        return handleMovedTo(cookie, name); // needed even for unsupported target names
    if (!isRelevantName(name))
        return true;
    // Events are queued in order, so any later event for a name means an
    // IN_MOVED_FROM still open for that name was a move out of the folder.
    if (!flushMovesFor(name))
        return false;

    if (mask & IN_CREATE) {
        if (!m_known.contains(name) && !m_pending.contains(name) && statOf(name))
            markPending(name);
    } else if (mask & IN_MODIFY) {
        if (m_pending.contains(name))
            markPending(name); // restarts the settle time
        else if (!m_known.contains(name) && statOf(name))
            markPending(name);
    } else if (mask & IN_CLOSE_WRITE) {
        m_pending.remove(name);
        return complete(name);
    } else if (mask & IN_DELETE) {
        m_pending.remove(name);
        if (m_known.remove(name)) {
            emit fileRemoved(name);
            return alive(gen);
        }
    } else if (mask & IN_MOVED_FROM) {
        Move mv;
        mv.name = name;
        if (m_known.remove(name))
            mv.wasKnown = true;
        else if (!m_pending.remove(name))
            return true; // untracked: nothing to pair
        mv.deadline = QDeadlineTimer(kMoveGraceMs, Qt::PreciseTimer);
        m_moves.insert(cookie, mv);
    }
    return true;
}

bool DirWatcher::handleMovedTo(quint32 cookie, const QString &name) {
    const quint64 gen = m_generation;
    std::optional<Move> mv;
    if (auto it = m_moves.find(cookie); it != m_moves.end()) {
        mv = it.value();
        m_moves.erase(it);
    }
    const bool relevant = isRelevantName(name);
    if (relevant && !flushMovesFor(name))
        return false;
    const std::optional<DiskFile> st = relevant ? statOf(name) : std::nullopt;

    if (mv) {
        if (st) {
            if (mv->wasKnown) {
                m_pending.remove(name);
                m_known.insert(name); // may replace an existing known name
                emit fileRenamed(mv->name, *st);
                return alive(gen);
            }
            markPending(name); // still being written under its new name
            return true;
        }
        if (mv->wasKnown) {
            emit fileRemoved(mv->name);
            if (!alive(gen))
                return false;
        }
    } else if (st) {
        // Moved in from elsewhere or from an unsupported name: complete now.
        m_pending.remove(name);
        return complete(name);
    }
    // The target is not a supported regular file (anymore).
    if (relevant) {
        m_pending.remove(name);
        if (m_known.remove(name)) {
            emit fileRemoved(name);
            return alive(gen);
        }
    }
    return true;
}

bool DirWatcher::flushMovesFor(const QString &name) {
    const quint64 gen = m_generation;
    for (auto it = m_moves.begin(); it != m_moves.end();) {
        if (it->name != name) {
            ++it;
            continue;
        }
        const bool wasKnown = it->wasKnown;
        it = m_moves.erase(it);
        if (wasKnown) {
            emit fileRemoved(name);
            if (!alive(gen))
                return false;
        }
    }
    return true;
}

// Reports a file whose content is complete: added if new, changed if known.
bool DirWatcher::complete(const QString &name) {
    const quint64 gen = m_generation;
    const std::optional<DiskFile> st = statOf(name);
    if (!st)
        return true; // gone again or not a regular file: later events tell
    if (m_known.contains(name)) {
        emit fileChanged(*st);
    } else {
        m_known.insert(name);
        emit fileAdded(*st);
    }
    return alive(gen);
}

void DirWatcher::markPending(const QString &name) {
    m_pending[name].deadline = QDeadlineTimer(kSettleMs, Qt::PreciseTimer);
    scheduleSettle();
}

void DirWatcher::scheduleSettle() {
    if (m_pending.isEmpty()) {
        m_settleTimer.stop();
        return;
    }
    qint64 next = kSettleMs;
    for (const Pending &p : std::as_const(m_pending))
        next = std::min(next, p.deadline.remainingTime());
    m_settleTimer.start(int(std::max<qint64>(next, 0)));
}

void DirWatcher::scheduleMoves() {
    if (m_moves.isEmpty()) {
        m_moveTimer.stop();
        return;
    }
    qint64 next = kMoveGraceMs;
    for (const Move &m : std::as_const(m_moves))
        next = std::min(next, m.deadline.remainingTime());
    m_moveTimer.start(int(std::max<qint64>(next, 0)));
}

void DirWatcher::onSettleTimeout() {
    // Drain the inotify queue first: a CLOSE_WRITE/DELETE may be waiting.
    const quint64 gen = m_generation;
    readEvents();
    if (!alive(gen))
        return;
    QStringList due;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it->deadline.hasExpired())
            due.push_back(it.key());
    for (const QString &name : std::as_const(due)) {
        if (!m_pending.remove(name))
            continue;
        if (!complete(name))
            return;
    }
    scheduleSettle();
}

void DirWatcher::onMoveTimeout() {
    const quint64 gen = m_generation;
    readEvents();
    if (!alive(gen))
        return;
    QList<quint32> due;
    for (auto it = m_moves.cbegin(); it != m_moves.cend(); ++it)
        if (it->deadline.hasExpired())
            due.push_back(it.key());
    for (quint32 cookie : std::as_const(due)) {
        auto it = m_moves.find(cookie);
        if (it == m_moves.end())
            continue;
        const Move mv = it.value();
        m_moves.erase(it);
        if (mv.wasKnown) {
            emit fileRemoved(mv.name);
            if (!alive(gen))
                return;
        }
    }
    scheduleMoves();
}

} // namespace picsel
