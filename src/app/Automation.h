// SPDX-License-Identifier: GPL-3.0-or-later
// Developer automation: when PICSEL_AUTOMATION=<script> is set, the script is
// played against the first window picsel shows (main, setup, error or
// folder picker window). Events are sent straight to that
// QWindow, so they never reach other applications.
//
// Script commands, one per line (# comments allowed):
//   key <sequence>      e.g. "key Right", "key S", "key Ctrl+Z"
//   click <x> <y>       left click at window coordinates (logical pixels)
//   dclick <x> <y>      double click
//   drag <x1> <y1> <x2> <y2>  left-button drag (e.g. to select text)
//   clipboard           log the clipboard's text
//   wait <ms>
//   shot <file.png>     grab the window contents
//   resize <w> <h>
//   nextwindow          continue in the next window picsel shows (e.g. the
//                       main window, after accepting a setup window)
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
    void moveTo(QQuickWindow *window);

    static QPointer<Automation> s_waiting; // after "nextwindow"
    // The last window shown: a click can open one before "nextwindow" runs.
    static QPointer<QQuickWindow> s_newest;
};

} // namespace picsel
