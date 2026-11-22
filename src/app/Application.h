// SPDX-License-Identifier: GPL-3.0-or-later
// Application: owns the QML engine and one window per open folder, and makes
// picsel single-instance (later launches forward their path over a socket).
#pragma once

#include <QJSEngine>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QtQmlIntegration>

namespace picsel {

class Session;

class Application : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    static Application *instance() { return s_instance; }
    static Application *create(QQmlEngine *, QJSEngine *engine);

    // Sends |path| to a running instance. Returns true if one took it.
    static bool forwardToRunningInstance(const QString &path);
    bool listen();

    // Opens a file's folder (highlighting the file) or a folder; empty shows a picker.
    void openPath(const QString &path, const QByteArray &activationToken = {});

    Q_INVOKABLE void openFolder(const QString &pathOrUrl);
    // Picker cancelled or error window closed.
    Q_INVOKABLE void dialogClosed();
    Q_INVOKABLE void setupAccepted(const QString &sourceDir, const QString &targetDir,
                                   bool defaultSelected, const QString &highlight);
    Q_INVOKABLE void setupCancelled();
    Q_INVOKABLE bool isWritableDir(const QString &pathOrUrl) const;
    Q_INVOKABLE QString toLocalPath(const QString &pathOrUrl) const;

private:
    struct Open {
        Session *session;
        QPointer<QQuickWindow> window;
    };

    void onNewConnection();
    void showPicker();
    void showSetup(const QString &dir, const QString &highlight);
    void showMainWindow(Session *session, const QString &highlight);
    void showError(const QString &message);
    QObject *createWindow(const char *type, const QVariantMap &props);
    void raise(QQuickWindow *w, const QByteArray &token);
    void quitIfIdle();

    static Application *s_instance;
    QQmlApplicationEngine m_engine;
    QLocalServer m_server;
    QVector<Open> m_open;
    int m_pendingWindows = 0; // setup/picker/error windows currently shown
    QHash<QString, QPointer<QQuickWindow>> m_setupWindows; // by source folder
};

} // namespace picsel
