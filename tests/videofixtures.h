// Test videos generated at test time with the ffmpeg CLI.
#pragma once

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace fixtures {

inline QString ffmpeg() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

inline bool run(const QStringList &args, QString *err = nullptr) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ffmpeg(), QStringList{QStringLiteral("-y"), QStringLiteral("-nostdin"), QStringLiteral("-v"),
                                  QStringLiteral("error")} + args);
    if (!p.waitForFinished(120000)) {
        p.kill();
        p.waitForFinished();
        if (err)
            *err = QStringLiteral("ffmpeg timed out");
        return false;
    }
    if (err)
        *err = QString::fromLocal8Bit(p.readAll());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

struct Spec {
    QString size = QStringLiteral("320x240");
    double seconds = 2.5;
    QString creation = QStringLiteral("2026-11-21T17:30:00Z"); // empty: none
    int displayRotationCcw = 0; // ffmpeg -display_rotation (counter-clockwise)
    bool faststart = false;
    bool audio = true;
    QString source = QStringLiteral("testsrc");
    QString filter; // appended to the lavfi source graph (",filter")
};

// Creates |out| (container from the extension). Tries libx264, then mpeg4.
inline bool makeVideo(const QString &out, const Spec &s, QString *err = nullptr) {
    const QString tmp = out + QStringLiteral(".plain") + out.mid(out.lastIndexOf(QLatin1Char('.')));
    for (const char *codec : {"libx264", "mpeg4"}) {
        QStringList a{QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      s.source + (s.source.contains(QLatin1Char('=')) ? QLatin1Char(':') : QLatin1Char('='))
                          + QStringLiteral("size=%1:rate=25").arg(s.size)
                          + (s.filter.isEmpty() ? QString() : QLatin1Char(',') + s.filter)};
        if (s.audio)
            a << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
              << QStringLiteral("sine=frequency=440");
        a << QStringLiteral("-t") << QString::number(s.seconds) << QStringLiteral("-c:v")
          << QString::fromLatin1(codec) << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
        if (s.audio)
            a << QStringLiteral("-c:a") << QStringLiteral("aac");
        if (!s.creation.isEmpty())
            a << QStringLiteral("-metadata") << QStringLiteral("creation_time=") + s.creation;
        if (s.faststart)
            a << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        a << (s.displayRotationCcw ? tmp : out);
        if (!run(a, err))
            continue;
        if (!s.displayRotationCcw)
            return true;
        // Remux with a display matrix (stream copy, so nothing gets rotated).
        QStringList r{QStringLiteral("-display_rotation"), QString::number(s.displayRotationCcw),
                      QStringLiteral("-i"), tmp, QStringLiteral("-c"), QStringLiteral("copy")};
        if (s.faststart)
            r << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        r << out;
        const bool ok = run(r, err);
        QFile::remove(tmp);
        return ok;
    }
    return false;
}

} // namespace fixtures
