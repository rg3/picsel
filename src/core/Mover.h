// Mover: moves the selected files from the source folder to the target folder.
//
// Everything works on directory file descriptors with *at() syscalls. On the
// same filesystem a file is moved with renameat2(RENAME_NOREPLACE); across
// filesystems it is copied to a temporary file in the target folder, synced,
// renamed into place and only then removed from the source, so the source is
// never deleted unless the destination is complete.
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace picsel {

enum class ConflictAction { Overwrite, Rename, Skip };

// Prefix of the temporary files used for cross-filesystem copies.
inline constexpr const char *kMoveTempPrefix = ".picsel-tmp-";

struct PreflightResult {
    QString fatal;          // non-empty: whole move impossible (target dir missing/not a dir/not writable, target == source dir, source unreadable)
    QStringList blocked;    // target name exists and is NOT a regular file (dir, symlink, fifo, socket, device): the whole move must be refused
    QStringList conflicts;  // target name exists as a regular file (will need a ConflictAction)
    bool ok() const { return fatal.isEmpty() && blocked.isEmpty(); }
};

// Checks every name before anything is moved. Uses lstat (symlinks count as
// non-regular). A name that is not a plain file name (empty, ".", "..", or
// containing '/') makes the result fatal.
PreflightResult preflightMove(const QString &srcDir, const QString &dstDir, const QStringList &names);

// First free name in dstDir of the form "stem-N.ext" (N = 1, 2, …), keeping the extension's case.
// "IMG_1.JPG" -> "IMG_1-1.JPG"; no extension -> "name-1"; a leading-dot name like ".x" has no extension.
QString suffixedName(const QString &dstDir, const QString &name);

struct MoveOutcome {
    enum Status { Moved, Skipped, Failed };
    QString name;       // source file name
    Status status = Failed;
    QString destName;   // final name in dstDir when Moved (also set when Failed after a complete cross-fs copy whose source could not be removed)
    QString error;      // when Failed
};

// Runs the move on its own worker thread. Signals are emitted from that thread
// (receivers in the GUI thread get them queued).
class MoveJob : public QObject {
    Q_OBJECT
public:
    MoveJob(const QString &srcDir, const QString &dstDir, const QStringList &names, QObject *parent = nullptr);
    ~MoveJob() override;           // cancels and joins the worker
    void start();                  // returns immediately; a second call does nothing
    // Answer to conflictFound(); callable from any thread. applyToAll = use this action for later conflicts without asking.
    void resolveConflict(ConflictAction action, bool applyToAll);
    void cancel();                 // stop after the current file; a pending conflict counts as Skip
signals:
    // Emitted before each file (done = files processed so far) and once at the
    // end with done = number of processed files (== total unless cancelled).
    void progress(int done, int total, const QString &currentName);
    void conflictFound(const QString &name);   // worker blocks until resolveConflict()/cancel()
    void fileMoved(const QString &name, const QString &destName);
    void finished(const QVector<picsel::MoveOutcome> &outcomes, bool cancelled);

private:
    void run();
    MoveOutcome moveOne(int srcFd, int dstFd, const QString &name);
    // Asks the user (or uses the apply-to-all answer). Returns Skip when cancelled.
    ConflictAction askConflict(const QString &name);

    const QString m_srcDir;
    const QString m_dstDir;
    const QStringList m_names;

    std::thread m_thread;
    std::atomic_bool m_cancel{false};
    std::mutex m_mutex;                     // protects the conflict state below
    std::condition_variable m_cond;
    bool m_waiting = false;                 // worker is waiting for an answer
    std::optional<ConflictAction> m_answer; // answer to the pending conflict
    std::optional<ConflictAction> m_applyToAll;
    bool m_crossFs = false;                 // worker only: EXDEV seen, copy directly
};

} // namespace picsel

Q_DECLARE_METATYPE(picsel::MoveOutcome)
