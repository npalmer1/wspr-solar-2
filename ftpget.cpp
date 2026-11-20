#include "ftpget.h"

#include <QRegularExpression>
#include <QTcpSocket>

FtpGet::FtpGet(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { fail("timed out"); });
}

void FtpGet::get(const QUrl &url, int timeoutMs)
{
    m_url = url;
    if (url.scheme() != "ftp" || url.host().isEmpty() || url.path().isEmpty()) {
        // (queued, so finished() always comes after get() returns, as with QNetworkReply)
        QMetaObject::invokeMethod(this, [this] { fail("not an ftp:// address: " + m_url.toString()); },
                                  Qt::QueuedConnection);
        return;
    }
    m_ctl = new QTcpSocket(this);
    connect(m_ctl, &QTcpSocket::readyRead, this, &FtpGet::readControl);
    connect(m_ctl, &QTcpSocket::errorOccurred, this, [this] {
        if (!m_finished)
            fail(m_ctl->errorString());
    });
    m_timer.start(timeoutMs);
    m_ctl->connectToHost(url.host(), quint16(url.port(21)));
}

void FtpGet::send(const QByteArray &command)
{
    m_ctl->write(command + "\r\n");
}

// Replies are "ddd text" lines, or "ddd-text" ... "ddd text" when they run over several lines
void FtpGet::readControl()
{
    m_buffer += m_ctl->readAll();
    int nl;
    while (!m_finished && (nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromLatin1(m_buffer.left(nl)).trimmed();
        m_buffer.remove(0, nl + 1);
        if (line.size() < 3 || !line.at(0).isDigit())
            continue;   // the middle of a multi-line reply
        const QString code = line.left(3);
        const QChar sep = line.size() > 3 ? line.at(3) : QChar(' ');
        if (m_multiCode.isEmpty() && sep == '-') {
            m_multiCode = code;   // more lines to come
            continue;
        }
        if (!m_multiCode.isEmpty()) {
            if (code != m_multiCode || sep != ' ')
                continue;
            m_multiCode.clear();
        }
        reply(code.toInt(), line.mid(4));
    }
}

void FtpGet::reply(int code, const QString &text)
{
    auto failWith = [&](const QString &what) { fail(QString("%1: %2 %3").arg(what).arg(code).arg(text)); };
    switch (m_step) {
    case Step::Greeting:
        if (code != 220)
            return failWith("server not ready");
        m_step = Step::User;
        send("USER anonymous");
        break;
    case Step::User:
        if (code == 230) {   // no password needed
            m_step = Step::Type;
            send("TYPE I");
        } else if (code == 331) {
            m_step = Step::Pass;
            send("PASS wspr-solar@");
        } else {
            failWith("login refused");
        }
        break;
    case Step::Pass:
        if (code != 230 && code != 202)
            return failWith("login refused");
        m_step = Step::Type;
        send("TYPE I");
        break;
    case Step::Type:
        if (code != 200)
            return failWith("TYPE I refused");
        m_step = Step::Epsv;
        send("EPSV");
        break;
    case Step::Epsv: {
        // 229 Entering Extended Passive Mode (|||port|)
        const auto m = QRegularExpression(R"(\(\|\|\|(\d+)\|\))").match(text);
        if (code == 229 && m.hasMatch()) {
            openData(quint16(m.captured(1).toUInt()));
        } else {   // not supported: the older PASV
            m_step = Step::Pasv;
            send("PASV");
        }
        break;
    }
    case Step::Pasv: {
        // 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2) - only the port is used (see ftpget.h)
        const auto m = QRegularExpression(R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))").match(text);
        if (code != 227 || !m.hasMatch())
            return failWith("passive mode refused");
        openData(quint16(m.captured(5).toUInt() * 256 + m.captured(6).toUInt()));
        break;
    }
    case Step::Retr:
        if (code == 150 || code == 125)
            break;   // the file is coming
        if (code == 226 || code == 250) {
            m_retrDone = true;
            checkComplete();
            break;
        }
        failWith("download refused");
        break;
    case Step::Done:
        break;
    }
}

void FtpGet::openData(quint16 port)
{
    m_step = Step::Retr;
    m_data = new QTcpSocket(this);
    connect(m_data, &QTcpSocket::connected, this, [this] { send("RETR " + m_url.path(QUrl::FullyEncoded).toLatin1()); });
    connect(m_data, &QTcpSocket::readyRead, this, [this] { m_file += m_data->readAll(); });
    auto closed = [this] {
        if (m_dataDone)
            return;
        m_file += m_data->readAll();
        m_dataDone = true;
        checkComplete();
    };
    connect(m_data, &QTcpSocket::disconnected, this, closed);
    connect(m_data, &QTcpSocket::errorOccurred, this, [this, closed](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError)
            closed();   // the normal end of the file
        else if (!m_finished)
            fail("data connection: " + m_data->errorString());
    });
    m_data->connectToHost(m_ctl->peerAddress(), port);
}

void FtpGet::checkComplete()
{
    if (m_dataDone && m_retrDone)
        finish(true, QString());
}

void FtpGet::fail(const QString &error)
{
    finish(false, error);
}

void FtpGet::finish(bool ok, const QString &error)
{
    if (m_finished)
        return;
    m_finished = true;
    m_step = Step::Done;
    m_timer.stop();
    if (m_ctl && m_ctl->state() == QAbstractSocket::ConnectedState) {
        send("QUIT");
        m_ctl->flush();
        m_ctl->disconnectFromHost();
    }
    if (m_data)
        m_data->abort();
    emit finished(ok, ok ? m_file : QByteArray(), error);
}
