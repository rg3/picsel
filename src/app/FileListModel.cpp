// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileListModel.h"

namespace picsel {

SortKey sortKeyOf(const Item &item) {
    return SortKey{item.name, item.sortTimeMs};
}

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {}

int FileListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    const Item *it = at(index.row());
    if (!it)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return it->name;
    case UidRole: return it->uid;
    case SelectedRole: return it->selected;
    case IsVideoRole: return it->kind == MediaKind::Video;
    case ThumbVersionRole: return it->thumbVersion;
    case ThumbFailedRole: return it->thumbFailed;
    case GroupIdRole: return it->groupId;
    case GroupStartRole: {
        const Item *prev = at(index.row() - 1);
        return it->groupId >= 0 && (!prev || prev->groupId != it->groupId);
    }
    case GroupEndRole: {
        const Item *next = at(index.row() + 1);
        return it->groupId >= 0 && (!next || next->groupId != it->groupId);
    }
    case DurationRole: return it->durationMs;
    }
    return {};
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    return {
        {NameRole, "name"},
        {UidRole, "uid"},
        {SelectedRole, "selected"},
        {IsVideoRole, "isVideo"},
        {ThumbVersionRole, "thumbVersion"},
        {ThumbFailedRole, "thumbFailed"},
        {GroupIdRole, "groupId"},
        {GroupStartRole, "groupStart"},
        {GroupEndRole, "groupEnd"},
        {DurationRole, "durationMs"},
    };
}

int FileListModel::rowOf(const QString &name) const {
    if (m_indexDirty) {
        m_index.clear();
        m_index.reserve(m_items.size());
        for (int i = 0; i < m_items.size(); ++i)
            m_index.insert(m_items[i].name, i);
        m_indexDirty = false;
    }
    return m_index.value(name, -1);
}

void FileListModel::setName(int row, const QString &name) {
    if (row < 0 || row >= m_items.size())
        return;
    m_items[row].name = name;
    invalidateIndex();
}

int FileListModel::rowOfUid(quint32 uid) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].uid == uid)
            return i;
    return -1;
}

void FileListModel::reset(QVector<Item> items) {
    beginResetModel();
    m_items = std::move(items);
    invalidateIndex();
    endResetModel();
    emit countChanged();
}

int FileListModel::sortedPosition(const Item &item, SortMode mode, int skipRow) const {
    // Binary search over the list without |skipRow|.
    const SortKey key = sortKeyOf(item);
    int lo = 0, hi = int(m_items.size()) - (skipRow >= 0 ? 1 : 0);
    auto get = [&](int i) -> const Item & {
        return m_items[(skipRow >= 0 && i >= skipRow) ? i + 1 : i];
    };
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (sortLess(sortKeyOf(get(mid)), key, mode))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int FileListModel::insertSorted(Item item, SortMode mode) {
    const int pos = sortedPosition(item, mode, -1);
    beginInsertRows({}, pos, pos);
    m_items.insert(pos, std::move(item));
    invalidateIndex();
    endInsertRows();
    // Neighbours' group start/end flags may change.
    if (pos > 0)
        notifyChanged(pos - 1, {GroupEndRole});
    if (pos + 1 < m_items.size())
        notifyChanged(pos + 1, {GroupStartRole});
    emit countChanged();
    return pos;
}

void FileListModel::removeRow(int row) {
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    invalidateIndex();
    endRemoveRows();
    if (row > 0)
        notifyChanged(row - 1, {GroupEndRole});
    if (row < m_items.size())
        notifyChanged(row, {GroupStartRole});
    emit countChanged();
}

int FileListModel::resort(int row, SortMode mode) {
    if (row < 0 || row >= m_items.size())
        return row;
    const int pos = sortedPosition(m_items[row], mode, row);
    if (pos != row) {
        // beginMoveRows uses "destination before which to insert" semantics.
        const int dest = pos > row ? pos + 1 : pos;
        beginMoveRows({}, row, row, {}, dest);
        m_items.move(row, pos);
        invalidateIndex();
        endMoveRows();
    }
    notifyChanged(pos, {});
    return pos;
}

void FileListModel::notifyChanged(int row, const QList<int> &roles) {
    if (row < 0 || row >= m_items.size())
        return;
    const QModelIndex i = index(row);
    emit dataChanged(i, i, roles);
}

void FileListModel::notifyAllChanged(const QList<int> &roles) {
    if (m_items.isEmpty())
        return;
    emit dataChanged(index(0), index(int(m_items.size()) - 1), roles);
}

} // namespace picsel
