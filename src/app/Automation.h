// SPDX-License-Identifier: GPL-3.0-or-later
// Developer automation: when PICSEL_AUTOMATION=<script> is set, the script is
// played against the first window picsel shows (the main window, or the
// folder picker when started without a path). Events are sent straight to that
// QWindow, so they never reach other applications.
//
// Script commands, one per line (# comments allowed):
//   key <sequence>      e.g. "key Right", "key S", "key Ctrl+Z"
//   click <x> <y>       left click at window coordinates (logical pixels)
//   dclick <x> <y>      double click
//   wait <ms>
//   shot <file.png>     grab the window contents
//   resize <w> <h>
//   quit
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStringList>

namespace picsel {

class Automation : public QObject {
    Q_OBJECT
public:
    static void startIfRequested(QQuickWindow *window);

private:
    Automation(QQuickWindow *window, QStringList lines);
    void step();

    QPointer<QQuickWindow> m_window;
    QStringList m_lines;
    int m_pos = 0;
};

} // namespace picsel
