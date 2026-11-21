#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "Mover.h"

#include <QFile>
#include <QRandomGenerator>

#include <cerrno>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace picsel {

namespace {

QByteArray enc(const QString &s) { return QFile::encodeName(s); }

QString errText(int e) { return QString::fromLocal8Bit(std::strerror(e)); }

// Owns a file descriptor.
class Fd {
public:
    explicit Fd(int fd = -1) : m_fd(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    int get() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }
    void reset(int fd = -1) {
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = fd;
    }
    // Closes and returns the close() error (0 on success).
    int close() {
        int e = 0;
        if (m_fd >= 0 && ::close(m_fd) != 0)
            e = errno;
        m_fd = -1;
        return e;
    }

private:
    int m_fd;
};

int openDir(const QString &path) {
    return ::open(enc(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

bool isPlainName(const QString &name) {
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
        && !name.contains(QLatin1Char('/')) && !name.contains(QChar(0));
}

// lstat relative to a directory fd. Returns 0 or the errno.
int lstatAt(int dfd, const QByteArray &name, struct stat *st) {
    return ::fstatat(dfd, name.constData(), st, AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno;
}

QString suffixedNameAt(int dfd, const QString &name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const bool hasExt = dot > 0 && dot < name.size() - 1;
    const QString stem = hasExt ? name.left(dot) : name;
    const QString ext = hasExt ? name.mid(dot) : QString();
    for (int n = 1; n < 1000000; ++n) {
        const QString candidate = stem + QLatin1Char('-') + QString::number(n) + ext;
        struct stat st;
        if (lstatAt(dfd, enc(candidate), &st) == ENOENT)
            return candidate;
    }
    return {};
}

// renameat2(RENAME_NOREPLACE). Filesystems that do not support the flag get
// an equivalent link + unlink. Returns 0 or the errno.
int renameNoReplace(int srcFd, const QByteArray &src, int dstFd, const QByteArray &dst) {
    if (::renameat2(srcFd, src.constData(), dstFd, dst.constData(), RENAME_NOREPLACE) == 0)
        return 0;
    const int e = errno;
    if (e != EINVAL && e != ENOSYS)
        return e;
    if (::linkat(srcFd, src.constData(), dstFd, dst.constData(), 0) != 0)
        return errno == EPERM || errno == EOPNOTSUPP ? e : errno;
    if (::unlinkat(srcFd, src.constData(), 0) != 0) {
        const int ue = errno;
        ::unlinkat(dstFd, dst.constData(), 0);
        return ue;
    }
    return 0;
}

// Copies all data from |in| to |out|. Returns 0 or the errno.
int copyData(int in, int out) {
    off_t inOff = 0, outOff = 0;
    bool useCfr = true;
    std::vector<char> buf;
    for (;;) {
        if (useCfr) {
            const ssize_t n = ::copy_file_range(in, &inOff, out, &outOff, 1 << 30, 0);
            if (n > 0)
                continue;
            if (n == 0)
                return 0;
            const int e = errno;
            if (e == EINTR)
                continue;
            if (e == EXDEV || e == ENOSYS || e == EINVAL || e == EOPNOTSUPP) {
                useCfr = false;
                continue;
            }
            return e;
        }
        if (buf.empty())
            buf.resize(1 << 20);
        const ssize_t n = ::pread(in, buf.data(), buf.size(), inOff);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return errno;
        }
        if (n == 0)
            return 0;
        inOff += n;
        ssize_t written = 0;
        while (written < n) {
            const ssize_t w = ::pwrite(out, buf.data() + written, size_t(n - written), outOff);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return errno;
            }
            written += w;
            outOff += w;
        }
    }
}

MoveOutcome failed(const QString &name, const QString &error) {
    MoveOutcome o;
    o.name = name;
    o.status = MoveOutcome::Failed;
    o.error = error;
    return o;
}

MoveOutcome skipped(const QString &name) {
    MoveOutcome o;
    o.name = name;
    o.status = MoveOutcome::Skipped;
    return o;
}

MoveOutcome moved(const QString &name, const QString &dest) {
    MoveOutcome o;
    o.name = name;
    o.status = MoveOutcome::Moved;
    o.destName = dest;
    return o;
}

QString notRegularError(const QString &dest) {
    return QStringLiteral("Target \"%1\" exists and is not a regular file").arg(dest);
}

// Checks that an Overwrite target is a regular file or absent.
// Returns an error message, empty when fine.
QString checkOverwriteTarget(int dstFd, const QString &dest) {
    struct stat st;
    const int e = lstatAt(dstFd, enc(dest), &st);
    if (e == 0)
        return S_ISREG(st.st_mode) ? QString() : notRegularError(dest);
    if (e == ENOENT)
        return {};
    return QStringLiteral("Cannot check target \"%1\": %2").arg(dest, errText(e));
}

} // namespace

PreflightResult preflightMove(const QString &srcDir, const QString &dstDir, const QStringList &names) {
    PreflightResult r;
    Fd srcFd(openDir(srcDir));
    if (!srcFd.valid()) {
        r.fatal = QStringLiteral("Cannot open the source folder \"%1\": %2").arg(srcDir, errText(errno));
        return r;
    }
    struct stat dst;
    if (::stat(enc(dstDir).constData(), &dst) != 0) {
        const int e = errno;
        r.fatal = e == ENOENT
            ? QStringLiteral("The target folder \"%1\" does not exist").arg(dstDir)
            : QStringLiteral("Cannot access the target folder \"%1\": %2").arg(dstDir, errText(e));
        return r;
    }
    if (!S_ISDIR(dst.st_mode)) {
        r.fatal = QStringLiteral("The target \"%1\" is not a folder").arg(dstDir);
        return r;
    }
    Fd dstFd(openDir(dstDir));
    if (!dstFd.valid()) {
        r.fatal = QStringLiteral("Cannot open the target folder \"%1\": %2").arg(dstDir, errText(errno));
        return r;
    }
    if (::faccessat(AT_FDCWD, enc(dstDir).constData(), W_OK | X_OK, AT_EACCESS) != 0) {
        r.fatal = QStringLiteral("The target folder \"%1\" is not writable: %2").arg(dstDir, errText(errno));
        return r;
    }
    struct stat src;
    if (::fstat(srcFd.get(), &src) != 0 || ::fstat(dstFd.get(), &dst) != 0) {
        r.fatal = QStringLiteral("Cannot stat the folders: %1").arg(errText(errno));
        return r;
    }
    if (src.st_dev == dst.st_dev && src.st_ino == dst.st_ino) {
        r.fatal = QStringLiteral("The target folder is the source folder");
        return r;
    }
    for (const QString &name : names) {
        if (!isPlainName(name)) {
            r.fatal = QStringLiteral("Invalid file name \"%1\"").arg(name);
            return r;
        }
        struct stat st;
        const int e = lstatAt(dstFd.get(), enc(name), &st);
        if (e == ENOENT)
            continue;
        if (e == 0 && S_ISREG(st.st_mode))
            r.conflicts << name;
        else
            r.blocked << name; // non-regular, or cannot be checked
    }
    return r;
}

QString suffixedName(const QString &dstDir, const QString &name) {
    Fd dfd(::open(enc(dstDir).constData(), O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (!dfd.valid())
        return {};
    return suffixedNameAt(dfd.get(), name);
}

MoveJob::MoveJob(const QString &srcDir, const QString &dstDir, const QStringList &names, QObject *parent)
    : QObject(parent), m_srcDir(srcDir), m_dstDir(dstDir), m_names(names) {
    qRegisterMetaType<picsel::MoveOutcome>();
    qRegisterMetaType<QVector<picsel::MoveOutcome>>();
}

MoveJob::~MoveJob() {
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void MoveJob::start() {
    if (m_thread.joinable())
        return;
    m_thread = std::thread([this] { run(); });
}

void MoveJob::resolveConflict(ConflictAction action, bool applyToAll) {
    {
        std::lock_guard lk(m_mutex);
        if (!m_waiting || m_answer)
            return;
        m_answer = action;
        if (applyToAll)
            m_applyToAll = action;
    }
    m_cond.notify_all();
}

void MoveJob::cancel() {
    {
        std::lock_guard lk(m_mutex);
        m_cancel = true;
    }
    m_cond.notify_all();
}

ConflictAction MoveJob::askConflict(const QString &name) {
    {
        std::lock_guard lk(m_mutex);
        if (m_applyToAll)
            return *m_applyToAll;
        if (m_cancel)
            return ConflictAction::Skip;
        m_answer.reset();
        m_waiting = true;
    }
    // A direct-connected receiver may answer from inside this emit.
    emit conflictFound(name);
    std::unique_lock lk(m_mutex);
    m_cond.wait(lk, [this] { return m_answer.has_value() || m_cancel.load(); });
    m_waiting = false;
    const ConflictAction a = m_answer.value_or(ConflictAction::Skip);
    m_answer.reset();
    return a;
}

void MoveJob::run() {
    const int total = int(m_names.size());
    QVector<MoveOutcome> outcomes;
    outcomes.reserve(total);

    Fd srcFd(openDir(m_srcDir));
    QString dirError;
    if (!srcFd.valid())
        dirError = QStringLiteral("Cannot open the source folder: %1").arg(errText(errno));
    Fd dstFd;
    if (dirError.isEmpty()) {
        dstFd.reset(openDir(m_dstDir));
        if (!dstFd.valid())
            dirError = QStringLiteral("Cannot open the target folder: %1").arg(errText(errno));
    }
    if (dirError.isEmpty()) {
        struct stat a, b;
        if (::fstat(srcFd.get(), &a) == 0 && ::fstat(dstFd.get(), &b) == 0
            && a.st_dev == b.st_dev && a.st_ino == b.st_ino)
            dirError = QStringLiteral("The target folder is the source folder");
    }

    for (int i = 0; i < total; ++i) {
        if (m_cancel)
            break;
        const QString &name = m_names.at(i);
        emit progress(i, total, name);
        MoveOutcome o = dirError.isEmpty() ? moveOne(srcFd.get(), dstFd.get(), name)
                                           : failed(name, dirError);
        outcomes.append(o);
        if (o.status == MoveOutcome::Moved)
            emit fileMoved(o.name, o.destName);
    }
    emit progress(int(outcomes.size()), total, QString());
    emit finished(outcomes, m_cancel.load());
}

MoveOutcome MoveJob::moveOne(int srcFd, int dstFd, const QString &name) {
    if (!isPlainName(name))
        return failed(name, QStringLiteral("Invalid file name"));
    const QByteArray src = enc(name);

    struct stat st;
    int e = lstatAt(srcFd, src, &st);
    if (e != 0)
        return failed(name, QStringLiteral("Cannot access the source file: %1").arg(errText(e)));
    if (!S_ISREG(st.st_mode))
        return failed(name, QStringLiteral("The source is not a regular file"));

    // Decide about an existing target before doing anything (and before any
    // copy). NOREPLACE below still catches targets that appear later.
    std::optional<ConflictAction> action;
    e = lstatAt(dstFd, src, &st);
    if (e == 0) {
        if (!S_ISREG(st.st_mode))
            return failed(name, notRegularError(name));
        action = askConflict(name);
        if (*action == ConflictAction::Skip)
            return skipped(name);
    } else if (e != ENOENT) {
        return failed(name, QStringLiteral("Cannot check the target: %1").arg(errText(e)));
    }

    // Places |fromName| (in |fromFd|) at its final name, following |action|.
    // Returns the outcome, or nothing when the rename failed with EXDEV.
    auto place = [&](int fromFd, const QByteArray &fromName) -> std::optional<MoveOutcome> {
        for (int attempt = 0; attempt < 10000; ++attempt) {
            const bool replace = action == ConflictAction::Overwrite;
            const QString dest = action == ConflictAction::Rename ? suffixedNameAt(dstFd, name) : name;
            if (dest.isEmpty())
                return failed(name, QStringLiteral("No free name found for the target"));
            const QByteArray d = enc(dest);
            int r;
            if (replace) {
                const QString err = checkOverwriteTarget(dstFd, dest);
                if (!err.isEmpty())
                    return failed(name, err);
                r = ::renameat(fromFd, fromName.constData(), dstFd, d.constData()) == 0 ? 0 : errno;
            } else {
                r = renameNoReplace(fromFd, fromName, dstFd, d);
            }
            if (r == 0)
                return moved(name, dest);
            if (r == EXDEV)
                return std::nullopt;
            if (r == EEXIST && !replace) {
                if (action == ConflictAction::Rename)
                    continue; // lost a race for that suffix: try the next one
                // The target appeared since the check above.
                struct stat t;
                const int te = lstatAt(dstFd, d, &t);
                if (te == 0 && !S_ISREG(t.st_mode))
                    return failed(name, notRegularError(dest));
                action = askConflict(name);
                if (*action == ConflictAction::Skip)
                    return skipped(name);
                continue;
            }
            return failed(name, QStringLiteral("Cannot move to \"%1\": %2").arg(dest, errText(r)));
        }
        return failed(name, QStringLiteral("Too many attempts to find a free target name"));
    };

    if (!m_crossFs) {
        if (auto o = place(srcFd, src))
            return *o;
        m_crossFs = true;
    }

    // Different filesystem: copy to a temp file in the target, sync, rename
    // into place, sync the folder and only then remove the source.
    Fd in(::openat(srcFd, src.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!in.valid())
        return failed(name, QStringLiteral("Cannot open the source file: %1").arg(errText(errno)));
    if (::fstat(in.get(), &st) != 0)
        return failed(name, QStringLiteral("Cannot stat the source file: %1").arg(errText(errno)));
    if (!S_ISREG(st.st_mode))
        return failed(name, QStringLiteral("The source is not a regular file"));

    QByteArray tmp;
    Fd out;
    for (int attempt = 0; attempt < 100 && !out.valid(); ++attempt) {
        static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        tmp = kMoveTempPrefix;
        for (int k = 0; k < 6; ++k)
            tmp += chars[QRandomGenerator::global()->bounded(int(sizeof(chars) - 1))];
        out.reset(::openat(dstFd, tmp.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        if (!out.valid() && errno != EEXIST)
            return failed(name, QStringLiteral("Cannot create a temporary file in the target folder: %1").arg(errText(errno)));
    }
    if (!out.valid())
        return failed(name, QStringLiteral("Cannot create a temporary file in the target folder"));

    auto failTmp = [&](const QString &what, int err) {
        out.reset();
        ::unlinkat(dstFd, tmp.constData(), 0);
        return failed(name, QStringLiteral("%1: %2").arg(what, errText(err)));
    };

    if ((e = copyData(in.get(), out.get())) != 0)
        return failTmp(QStringLiteral("Cannot copy the file"), e);
    if (::fchmod(out.get(), st.st_mode & 07777) != 0)
        return failTmp(QStringLiteral("Cannot set the file mode"), errno);
    const struct timespec times[2] = {st.st_atim, st.st_mtim};
    if (::futimens(out.get(), times) != 0)
        return failTmp(QStringLiteral("Cannot set the file times"), errno);
    if (::fsync(out.get()) != 0)
        return failTmp(QStringLiteral("Cannot sync the copy"), errno);
    if ((e = out.close()) != 0)
        return failTmp(QStringLiteral("Cannot close the copy"), e);
    in.reset();

    std::optional<MoveOutcome> o = place(dstFd, tmp);
    if (!o) // EXDEV within one folder: should not happen
        return failTmp(QStringLiteral("Cannot rename the copy"), EXDEV);
    if (o->status != MoveOutcome::Moved) {
        ::unlinkat(dstFd, tmp.constData(), 0);
        return *o;
    }
    if (::fsync(dstFd) != 0) {
        // The copy is in place but may not be durable: keep the source.
        return failed(name, QStringLiteral("Copied to \"%1\" but cannot sync the target folder (source kept): %2")
                                .arg(o->destName, errText(errno)));
    }
    if (::unlinkat(srcFd, src.constData(), 0) != 0) {
        MoveOutcome f = failed(name, QStringLiteral("Copied to \"%1\" but cannot remove the source: %2")
                                         .arg(o->destName, errText(errno)));
        f.destName = o->destName;
        return f;
    }
    return *o;
}

} // namespace picsel
