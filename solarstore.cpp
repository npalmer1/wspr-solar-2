// WSPR Solar 2 - see solarstore.h.
#include "solarstore.h"

#include <QAtomicInt>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace {

QAtomicInt g_next;

const char *prefix(SolarStore::Column c)
{
    switch (c) {
    case SolarStore::Column::ProtonFlux: return "pf";
    case SolarStore::Column::Flares: return "fl";
    default: return "s";
    }
}

const char *kHours[8] = {"00", "03", "06", "09", "12", "15", "18", "21"};

QVariant numberOrNull(const QString &v)
{
    bool ok = false;
    const double d = v.trimmed().toDouble(&ok);
    return ok ? QVariant(d) : QVariant(QMetaType::fromType<double>());
}

QVariant intOrNull(const QString &v)
{
    bool ok = false;
    const double d = v.trimmed().toDouble(&ok);
    return ok ? QVariant(qRound(d)) : QVariant(QMetaType::fromType<int>());
}

QString dayText(const QDate &d)
{
    return d.toString("yyyy-MM-dd") + " 00:00:00";
}

} // namespace

SolarStore::SolarStore(const DbSettings &s, const QString &dbName)
    : m_conn(QString("solar_%1").arg(g_next.fetchAndAddRelaxed(1))), m_s(s), m_db(dbName)
{
}

SolarStore::~SolarStore()
{
    if (QSqlDatabase::contains(m_conn)) {
        { QSqlDatabase::database(m_conn, false).close(); }
        QSqlDatabase::removeDatabase(m_conn);
    }
}

bool SolarStore::open()
{
    QSqlDatabase d = QSqlDatabase::contains(m_conn) ? QSqlDatabase::database(m_conn, false)
                                                     : QSqlDatabase::addDatabase("QMYSQL", m_conn);
    if (!d.isValid()) {
        m_error = "The Qt MySQL driver (QMYSQL) isn't installed";
        return false;
    }
    if (d.isOpen()) {
        QSqlQuery ping(d);
        if (ping.exec("SELECT 1"))
            return true;
        d.close();   // the server went away: connect again
    }
    d.setHostName(m_s.host);
    d.setPort(m_s.port);
    d.setUserName(m_s.user);
    d.setPassword(m_s.password);
    d.setDatabaseName(m_db);
    d.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=3");
    if (!d.open()) {
        m_error = d.lastError().text();
        return false;
    }
    return true;
}

bool SolarStore::exec(const QString &sql, const QVariantList &args)
{
    if (!open())
        return false;
    QSqlQuery q(QSqlDatabase::database(m_conn, false));
    q.prepare(sql);
    for (const QVariant &a : args)
        q.addBindValue(a);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return false;
    }
    return true;
}

bool SolarStore::widenColumns()
{
    if (!open())
        return false;
    QSqlQuery q(QSqlDatabase::database(m_conn, false));
    q.prepare("SELECT CHARACTER_MAXIMUM_LENGTH FROM information_schema.COLUMNS "
              "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = 'weather' AND COLUMN_NAME = 's00'");
    q.addBindValue(m_db);
    if (!q.exec() || !q.next()) {
        m_error = q.lastError().text();
        return false;
    }
    if (q.value(0).toInt() >= kSlotWidth)
        return true;
    QStringList mods;
    for (const char *p : {"pf", "fl", "s"})
        for (const char *h : kHours)
            mods << QString("MODIFY `%1%2` VARCHAR(%3) DEFAULT ''").arg(p, h).arg(kSlotWidth);
    return exec("ALTER TABLE weather " + mods.join(", "), {});
}

bool SolarStore::saveDefault(const QDate &day)
{
    return exec("INSERT IGNORE INTO weather(datetime) VALUES(?)", {dayText(day)});
}

bool SolarStore::saveGeo(const QDate &day, const Solar::Geo &g)
{
    QVariantList a{dayText(day), intOrNull(g.Ap)};
    for (const QString &k : g.K)
        a << numberOrNull(k);
    return exec("INSERT INTO weather(datetime, Ap, Kp00, Kp03, Kp06, Kp09, Kp12, Kp15, Kp18, Kp21) "
                "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE Ap = VALUES(Ap), "
                "Kp00 = VALUES(Kp00), Kp03 = VALUES(Kp03), Kp06 = VALUES(Kp06), Kp09 = VALUES(Kp09), "
                "Kp12 = VALUES(Kp12), Kp15 = VALUES(Kp15), Kp18 = VALUES(Kp18), Kp21 = VALUES(Kp21)",
                a);
}

bool SolarStore::saveSolar(const QDate &day, const Solar::Daily &d)
{
    return exec("INSERT INTO weather(datetime, flux, SSN, Xray) VALUES(?, ?, ?, ?) "
                "ON DUPLICATE KEY UPDATE flux = VALUES(flux), SSN = VALUES(SSN), Xray = VALUES(Xray)",
                {dayText(day), numberOrNull(d.flux), intOrNull(d.ssn), d.xray.left(10)});
}

bool SolarStore::saveSlots(const QDate &day, Column c, const std::array<QString, 8> &values)
{
    const QString p = prefix(c);
    QStringList cols, upd;
    QVariantList a{dayText(day)};
    for (int i = 0; i < 8; ++i) {
        const QString col = p + kHours[i];
        cols << col;
        upd << QString("%1 = VALUES(%1)").arg(col);
        a << values[i].left(kSlotWidth);
    }
    return exec(QString("INSERT INTO weather(datetime, %1) VALUES(?%2) ON DUPLICATE KEY UPDATE %3")
                    .arg(cols.join(", "), QString(", ?").repeated(8), upd.join(", ")),
                a);
}

QList<SolarStore::Row> SolarStore::rows(const Filter &f, int limit)
{
    QList<Row> out;
    if (!open())
        return out;
    QStringList where;
    QVariantList a;
    if (f.useDates) {
        where << "datetime >= ? AND datetime <= ?";
        a << dayText(f.from) << dayText(f.to);
    }
    if (f.apFrom >= 0) {
        where << "Ap >= ?";
        a << f.apFrom;
    }
    if (f.apTo >= 0) {
        where << "Ap <= ?";
        a << f.apTo;
    }
    if (f.ssnFrom >= 0) {
        where << "SSN >= ?";
        a << f.ssnFrom;
    }
    if (f.ssnTo >= 0) {
        where << "SSN <= ?";
        a << f.ssnTo;
    }
    if (!f.flareClass.isEmpty()) {
        QStringList any;
        for (const char *h : kHours) {
            any << QString("fl%1 LIKE ?").arg(h);
            a << "%/" + f.flareClass + "%";   // "03:21/C1.2/...": the class follows a "/"
        }
        where << "(" + any.join(" OR ") + ")";
    }
    QString sql = "SELECT * FROM weather";
    if (!where.isEmpty())
        sql += " WHERE " + where.join(" AND ");
    sql += QString(" ORDER BY datetime DESC LIMIT %1").arg(limit);
    QSqlQuery q(QSqlDatabase::database(m_conn, false));
    q.prepare(sql);
    for (const QVariant &v : a)
        q.addBindValue(v);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QSqlRecord r = q.record();
        Row row;
        row.date = q.value("datetime").toDateTime().date();
        row.Ap = q.value("Ap").toInt();
        for (int i = 0; i < 8; ++i) {
            row.K[i] = q.value(QString("Kp") + kHours[i]).toDouble();
            row.pf[i] = q.value(QString("pf") + kHours[i]).toString();
            row.fl[i] = q.value(QString("fl") + kHours[i]).toString();
            row.s[i] = q.value(QString("s") + kHours[i]).toString();
        }
        row.flux = q.value("flux").toDouble();
        row.SSN = q.value("SSN").toInt();
        row.xray = q.value("Xray").toString();
        out << row;
    }
    return out;
}

int SolarStore::count()
{
    if (!open())
        return 0;
    QSqlQuery q(QSqlDatabase::database(m_conn, false));
    return q.exec("SELECT COUNT(*) FROM weather") && q.next() ? q.value(0).toInt() : 0;
}

bool SolarStore::createTestDatabase()
{
    const QString name = m_conn + "_admin";
    bool ok = false;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase("QMYSQL", name);
        d.setHostName(m_s.host);
        d.setPort(m_s.port);
        d.setUserName(m_s.user);
        d.setPassword(m_s.password);
        d.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=3");
        if (!d.open()) {
            m_error = d.lastError().text();
        } else {
            QSqlQuery q(d);
            QStringList cols{"`datetime` datetime NOT NULL", "`Ap` int(11) DEFAULT 0"};
            for (const char *h : kHours)
                cols << QString("`Kp%1` float DEFAULT 0").arg(h);
            cols << "`flux` float DEFAULT 0" << "`SSN` int(11) DEFAULT 0" << "`Xray` varchar(10) DEFAULT ''";
            for (const char *p : {"pf", "fl", "s"})
                for (const char *h : kHours)
                    cols << QString("`%1%2` varchar(25) DEFAULT ''").arg(p, h);
            ok = q.exec("DROP DATABASE IF EXISTS `" + m_db + "`") && q.exec("CREATE DATABASE `" + m_db + "`")
                 && q.exec("CREATE TABLE `" + m_db + "`.weather (" + cols.join(", ") + ", PRIMARY KEY (`datetime`))");
            if (!ok)
                m_error = q.lastError().text();
            d.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

void SolarStore::dropTestDatabase()
{
    if (open()) {
        QSqlQuery q(QSqlDatabase::database(m_conn, false));
        q.exec("DROP DATABASE IF EXISTS `" + m_db + "`");
    }
}
