#pragma once
// Downloads one file over FTP (anonymous, passive mode) - Qt 6 has no FTP of its own, and NOAA's AK.txt
// is only published on its FTP server (C#: WebClient.DownloadString("ftp://...")).
//
//   auto *ftp = new FtpGet(this);
//   connect(ftp, &FtpGet::finished, this, [](bool ok, const QByteArray &data, const QString &error) { ... });
//   ftp->get(QUrl("ftp://ftp.swpc.noaa.gov/pub/lists/geomag/AK.txt"));
//
// finished() is emitted exactly once; the object can then be deleted (deleteLater). Passive mode (EPSV,
// else PASV) works through home routers and firewalls; the data connection always goes to the address
// of the control connection, ignoring the address in a PASV reply (which can be a private one).
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QTcpSocket;

class FtpGet : public QObject
{
    Q_OBJECT
public:
    explicit FtpGet(QObject *parent = nullptr);
    void get(const QUrl &url, int timeoutMs = 30000);

signals:
    void finished(bool ok, const QByteArray &data, const QString &error);

private:
    enum class Step { Greeting, User, Pass, Type, Epsv, Pasv, Retr, Done };
    void readControl();
    void reply(int code, const QString &text);
    void send(const QByteArray &command);
    void openData(quint16 port);
    void checkComplete();
    void fail(const QString &error);
    void finish(bool ok, const QString &error);

    QTcpSocket *m_ctl = nullptr;
    QTcpSocket *m_data = nullptr;
    QTimer m_timer;
    QUrl m_url;
    Step m_step = Step::Greeting;
    QByteArray m_buffer;     // control replies not yet complete
    QString m_multiCode;     // "220" while inside a "220-..." multi-line reply
    QByteArray m_file;
    bool m_dataDone = false;   // the server closed the data connection
    bool m_retrDone = false;   // ... and said 226 (transfer complete)
    bool m_finished = false;
};
