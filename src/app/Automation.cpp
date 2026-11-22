// SPDX-License-Identifier: GPL-3.0-or-later
#include "Automation.h"

#include <QCoreApplication>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTimer>

namespace picsel {

void Automation::startIfRequested(QQuickWindow *window) {
    static bool started = false;
    const QString path = qEnvironmentVariable("PICSEL_AUTOMATION");
    if (path.isEmpty() || started)
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "automation: cannot read" << path;
        return;
    }
    started = true;
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    auto *a = new Automation(window, std::move(lines));
    QTimer::singleShot(500, a, &Automation::step);
}

Automation::Automation(QQuickWindow *window, QStringList lines)
    : QObject(window), m_window(window), m_lines(std::move(lines)) {}

static void sendKey(QWindow *w, const QKeyCombination &kc) {
    const QString text = kc.keyboardModifiers() & ~Qt::ShiftModifier
        ? QString() : QKeySequence(kc.key()).toString().toLower();
    QKeyEvent press(QEvent::KeyPress, kc.key(), kc.keyboardModifiers(), text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, kc.key(), kc.keyboardModifiers(), text);
    QCoreApplication::sendEvent(w, &release);
}

static void sendClick(QWindow *w, QPointF pos, bool dbl) {
    const QPointF global = w->mapToGlobal(pos);
    auto send = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent ev(type, pos, pos, global, Qt::LeftButton, buttons, Qt::NoModifier,
                       QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(w, &ev);
    };
    send(QEvent::MouseMove, Qt::NoButton);
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    if (dbl) {
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::NoButton);
    }
}

void Automation::step() {
    if (!m_window)
        return;
    int delay = 30;
    while (m_pos < m_lines.size()) {
        const QString line = m_lines[m_pos++].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList a = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString cmd = a[0];
        qInfo().noquote() << "automation:" << line;
        if (cmd == QLatin1String("key") && a.size() > 1) {
            const QKeySequence seq = QKeySequence::fromString(a.mid(1).join(QLatin1Char(' ')));
            if (seq.isEmpty())
                qWarning() << "automation: bad key" << line;
            else
                sendKey(m_window, seq[0]);
        } else if ((cmd == QLatin1String("click") || cmd == QLatin1String("dclick")) && a.size() > 2) {
            sendClick(m_window, QPointF(a[1].toDouble(), a[2].toDouble()), cmd == QLatin1String("dclick"));
        } else if (cmd == QLatin1String("wait") && a.size() > 1) {
            delay = a[1].toInt();
            break;
        } else if (cmd == QLatin1String("shot") && a.size() > 1) {
            if (!m_window->grabWindow().save(a[1]))
                qWarning() << "automation: cannot save" << a[1];
        } else if (cmd == QLatin1String("resize") && a.size() > 2) {
            m_window->showNormal();
            m_window->resize(a[1].toInt(), a[2].toInt());
            delay = 300;
            break;
        } else if (cmd == QLatin1String("quit")) {
            QCoreApplication::quit();
            return;
        } else {
            qWarning() << "automation: unknown command" << line;
        }
        break; // one event per step so the UI can react
    }
    if (m_pos < m_lines.size())
        QTimer::singleShot(delay, this, &Automation::step);
}

} // namespace picsel
