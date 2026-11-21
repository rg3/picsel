// Session: everything about one open source folder (one window).
#pragma once

#include "FileListModel.h"
#include "core/DirWatcher.h"
#include "core/Metadata.h"
#include "core/Similarity.h"
#include "core/Mover.h"
#include "core/SelectionDb.h"
#include "core/Sorting.h"
#include "image/ImageCache.h"
#include "image/ImageProviders.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>
#include <memory>

namespace picsel {

class Session : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(int sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString sourceDir READ sourceDir CONSTANT)
    Q_PROPERTY(QString targetDir READ targetDir NOTIFY targetDirChanged)
    Q_PROPERTY(bool defaultSelected READ defaultSelected WRITE setDefaultSelected NOTIFY defaultSelectedChanged)
    Q_PROPERTY(QString sortMode READ sortModeString WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(FileListModel *model READ model CONSTANT)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)
    Q_PROPERTY(bool showSimilar READ showSimilar WRITE setShowSimilar NOTIFY similarChanged)
    Q_PROPERTY(int similarSeconds READ similarSeconds WRITE setSimilarSeconds NOTIFY similarChanged)
    Q_PROPERTY(int similarBits READ similarBits WRITE setSimilarBits NOTIFY similarChanged)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY groupsChanged)

public:
    explicit Session(const QString &dir, QObject *parent = nullptr);
    ~Session() override;

    // Opens an existing picsel.db, or creates one when |create| is set.
    bool open();
    bool create(const QString &targetDir, bool defaultSelected);
    QString errorString() const { return m_error; }

    int sessionId() const { return m_id; }
    QString sourceDir() const { return m_dir; }
    QString targetDir() const { return m_targetDir; }
    bool defaultSelected() const { return m_defaultSelected; }
    void setDefaultSelected(bool v);
    QString sortModeString() const { return sortModeToString(m_sortMode); }
    void setSortMode(const QString &mode);
    FileListModel *model() { return &m_model; }
    int selectedCount() const { return m_selectedCount; }
    int totalCount() const { return m_model.count(); }
    int columns() const { return m_columns; }
    void setColumns(int n);
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool moving() const { return m_moveJob != nullptr; }
    bool showSimilar() const { return m_showSimilar; }
    void setShowSimilar(bool on);
    int similarSeconds() const { return m_similarSeconds; }
    void setSimilarSeconds(int s);
    int similarBits() const { return m_similarBits; }
    void setSimilarBits(int b);
    int groupCount() const { return m_groupCount; }

    // Checks the selected files against the target folder. Returns
    // {count, total, target, fatal, blocked: [names], conflicts: [names]}.
    Q_INVOKABLE QVariantMap prepareMove() const;
    Q_INVOKABLE void startMove();
    // action: 0 = overwrite, 1 = keep both (rename), 2 = skip
    Q_INVOKABLE void resolveConflict(int action, bool applyToAll);
    Q_INVOKABLE void cancelMove();

    // Cameras seen in this folder: [{camera, count, offset (seconds)}].
    Q_INVOKABLE QVariantList cameras() const;
    // Global clock correction for a camera model; re-sorts when sorting by date.
    Q_INVOKABLE void setCameraOffset(const QString &camera, qint64 seconds);
    // Re-reads global camera offsets (e.g. changed in another window).
    void reloadCameraOffsets();

    Q_INVOKABLE int rowOf(const QString &name) const { return m_model.rowOf(name); }
    // The view's highlighted row; kept across re-sorts and list changes.
    Q_INVOKABLE void setCurrentRow(int row);
    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void toggleSelected(int row);
    // Reverts the last selection change; returns its row (or -1).
    Q_INVOKABLE int undo();
    Q_INVOKABLE bool setTargetDir(const QString &pathOrUrl);
    Q_INVOKABLE QVariantMap info(int row) const;

    // View feedback, used to prioritise decoding.
    Q_INVOKABLE void setThumbBox(int w, int h);
    Q_INVOKABLE void setVisibleRange(int first, int last);
    Q_INVOKABLE void setFullBox(int w, int h);
    Q_INVOKABLE void setFullFocus(int row); // -1 when the full view is closed

    // Asks the window to highlight |name| (from a second "open with").
    void requestHighlight(const QString &name);

signals:
    void targetDirChanged();
    void defaultSelectedChanged();
    void sortModeChanged();
    void countsChanged();
    void columnsChanged();
    void canUndoChanged();
    void highlightRequested(int row);
    void itemInfoChanged(int row);
    void errorOccurred(const QString &message);
    void movingChanged();
    void similarChanged();
    void groupsChanged();
    void moveProgress(int done, int total, const QString &name);
    void moveConflict(const QString &name);
    // Summary of a finished move, for the result message.
    void moveFinished(int moved, int skipped, const QStringList &failures, bool cancelled);

private:
    struct UndoEntry {
        QString name;
        bool previous;
    };

    bool loadFiles();
    Item makeItem(const DbFile &f);
    void rebuildCacheOrder();
    void scheduleCacheOrder();
    void onThumbReady(const QString &name, QSize fullSize);
    void startMetadataScan();
    void scheduleMetadata(const QStringList &names);
    void onFileAdded(const picsel::DiskFile &f);
    void recomputeGroups();
    void onFileRemoved(const QString &name);
    void onFileRenamed(const QString &from, const picsel::DiskFile &to);
    void onFileChanged(const picsel::DiskFile &f);
    void onRescanNeeded();
    void onMetadataRead(const QString &name, qint64 size, qint64 mtimeNs, const picsel::PhotoInfo &info);
    void flushMetaWrites();
    void resortAll();
    void onFileMoved(const QString &name);
    void onMoveFinished(const QVector<picsel::MoveOutcome> &outcomes, bool cancelled);
    void flushRemovals();
    void removeItem(const QString &name);
    void onThumbFailed(const QString &name);
    void recountSelected();
    bool applySelected(int row, bool selected, bool recordUndo);
    void fail(const QString &message);

    const int m_id;
    const QString m_dir;
    QString m_targetDir;
    bool m_defaultSelected = false;
    SortMode m_sortMode = SortMode::Name;
    int m_columns = 4;
    int m_selectedCount = 0;
    quint32 m_nextUid = 1;
    QString m_error;

    SelectionDb m_db;
    FileListModel m_model;
    std::shared_ptr<ImageCache> m_thumbs;
    std::shared_ptr<ImageCache> m_full;
    std::shared_ptr<UidMap> m_uids;
    QVector<UndoEntry> m_undo;
    QTimer m_cacheOrderTimer;
    QString m_currentName;
    QHash<QString, PhotoInfo> m_details;      // lens/exposure for the info panel
    QHash<QString, FileMeta> m_pendingMeta;   // to write to the DB in one transaction
    QTimer m_metaFlushTimer;
    QTimer m_resortTimer;
    QPointer<MoveJob> m_moveJob;
    DirWatcher m_watcher;
    QHash<QString, quint64> m_hashes;
    QTimer m_groupTimer;
    bool m_showSimilar = true;
    int m_similarSeconds = 10;
    int m_similarBits = 10;
    int m_groupCount = 0;
    QStringList m_pendingRemovals;   // moved files to drop from DB + model
    QTimer m_removalTimer;
    QThreadPool m_metaPool; // last: destroyed (and joined) first
};

} // namespace picsel
