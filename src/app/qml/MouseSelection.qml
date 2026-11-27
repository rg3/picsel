// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The SelectableLabel text field holding the current mouse selection.
// Not named TextSelection: QtQuick 6.11 has a type of that name, which
// silently wins at run time, so writes to |owner| went nowhere.
QtObject {
    property Item owner
}
