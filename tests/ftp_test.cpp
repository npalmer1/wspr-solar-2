// FtpGet against a local FTP server (tests/ftp_server.py, pyftpdlib) serving tests/data: the real AK.txt,
// a multi-line greeting, EPSV and the older PASV, a missing file, a server that never answers, and a
// bad address. Skipped (passes) if python3 or pyftpdlib isn't there.
#include "ftpget.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTcpServer>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
                         else std::printf("ok   %s\n", #cond); } while (0)

struct Result { bool ok = false; QByteArray data; QString error; int calls = 0; };

static Result download(const QUrl &url, int timeoutMs = 10000)
{
    Result r;
    FtpGet ftp;
    QEventLoop loop;
    QObject::connect(&ftp, &FtpGet::finished, [&](bool ok, const QByteArray &d, const QString &e) {
        r.ok = ok; r.data = d; r.error = e; ++r.calls;
        QTimer::singleShot(200, &loop, &QEventLoop::quit);   // (a little longer: a second finished() would show)
    });
    QTimer::singleShot(timeoutMs + 5000, &loop, &QEventLoop::quit);
    ftp.get(url, timeoutMs);
    loop.exec();
    return r;
}

// starts tests/ftp_server.py; returns its port, or 0
static quint16 startServer(QProcess &p, const QString &dataDir, const QString &mode)
{
    p.start("python3", {QString(SOURCE_DIR "/tests/ftp_server.py"), dataDir, mode});
    if (!p.waitForStarted(5000) || !p.waitForReadyRead(10000))
        return 0;
    return quint16(p.readLine().trimmed().toUInt());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString dataDir = QString(SOURCE_DIR "/tests/data");
    QFile akFile(dataDir + "/AK.txt");
    akFile.open(QIODevice::ReadOnly);
    const QByteArray ak = akFile.readAll();

    for (const QString mode : {"normal", "noepsv"}) {
        QProcess server;
        const quint16 port = startServer(server, dataDir, mode);
        if (!port) {
            std::printf("ftp_test: no test FTP server (python3 + pyftpdlib) - skipped\n%s\n",
                        server.readAllStandardError().constData());
            return 0;
        }
        std::printf("-- %s (port %u)\n", qPrintable(mode), port);
        const QString base = QString("ftp://127.0.0.1:%1/").arg(port);

        Result r = download(QUrl(base + "AK.txt"));
        CHECK(r.ok && r.error.isEmpty() && r.calls == 1);
        CHECK(r.data == ak && r.data.contains("Planetary("));   // byte for byte (TYPE I)

        r = download(QUrl(base + "no-such-file.txt"));
        CHECK(!r.ok && r.data.isEmpty() && r.calls == 1);
        CHECK(r.error.startsWith("download refused: 550"));

        // twice more, so a slow close of the last transfer doesn't spoil the next
        CHECK(download(QUrl(base + "AK.txt")).data == ak);
        CHECK(download(QUrl(base + "sgas.txt")).data.contains(":Product: Solar and Geophysical Activity Summary"));

        server.kill();
        server.waitForFinished(3000);
    }

    {   // a server that accepts the connection and never says anything: times out, once
        QTcpServer silent;
        silent.listen(QHostAddress::LocalHost);
        const Result r = download(QUrl(QString("ftp://127.0.0.1:%1/AK.txt").arg(silent.serverPort())), 1500);
        CHECK(!r.ok && r.error == "timed out" && r.calls == 1);
    }
    {   // nothing listening
        QTcpServer s;
        s.listen(QHostAddress::LocalHost);
        const quint16 port = s.serverPort();
        s.close();
        const Result r = download(QUrl(QString("ftp://127.0.0.1:%1/AK.txt").arg(port)));
        CHECK(!r.ok && !r.error.isEmpty() && r.calls == 1);
    }
    {   // not an ftp:// address
        const Result r = download(QUrl("https://example.com/AK.txt"));
        CHECK(!r.ok && r.error.startsWith("not an ftp:// address") && r.calls == 1);
    }

    std::printf(failures ? "ftp_test: %d FAILED\n" : "ftp_test: all passed\n", failures);
    return failures ? 1 : 0;
}
