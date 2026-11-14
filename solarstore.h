#pragma once
// WSPR Solar 2 - the wspr_sol database (table weather, one row a day). C#: savedefaultdata,
// saveGeoData, SaveSolardata, SavePFdata, SaveFlaredata, SaveBurstdata, find_data, find_extra_data,
// find_burst_data, table_count.
//
// Differences from the C# version:
//  - values are passed as query parameters (C# pasted them into the SQL text);
//  - the proton flux, flare and burst columns (pfNN, flNN, sNN) are widened from 25 to 300 characters
//    on start-up: the C# version wrote up to 300, and MariaDB / MySQL in their default strict mode
//    refused the whole row when a value was longer than the column, so on busy days nothing was saved;
//  - an empty number is saved as NULL (C# wrote '', which strict mode also refused);
//  - the Ap / SSN / date filters combine properly (C# left out an AND between the date and the Ap
//    range, so using both was an SQL error).
#include "solarcore.h"

#include <QDate>
#include <QList>
#include <QString>
#include <array>

struct DbSettings
{
    QString host = "127.0.0.1";
    int port = 3306;
    QString user = "admin";
    QString password = "wspr";
};

class SolarStore
{
public:
    explicit SolarStore(const DbSettings &s = DbSettings(), const QString &dbName = "wspr_sol");
    ~SolarStore();

    bool open();                 // connects (again if need be); false + lastError() if not
    QString lastError() const { return m_error; }
    const DbSettings &settings() const { return m_s; }

    bool widenColumns();         // pf/fl/s columns to 300 characters if they are narrower
    bool saveDefault(const QDate &day);                 // the day's row, if it isn't there yet
    bool saveGeo(const QDate &day, const Solar::Geo &g);   // Ap, Kp00..Kp21
    bool saveSolar(const QDate &day, const Solar::Daily &d);   // flux, SSN, Xray

    enum class Column { ProtonFlux, Flares, Bursts };   // pf, fl, s
    bool saveSlots(const QDate &day, Column c, const std::array<QString, 8> &values);

    struct Row
    {
        QDate date;
        int Ap = 0;
        std::array<double, 8> K{};
        double flux = 0;
        int SSN = 0;
        QString xray;
        std::array<QString, 8> pf, fl, s;
    };
    struct Filter
    {
        bool useDates = false;
        QDate from, to;
        int apFrom = -1, apTo = -1, ssnFrom = -1, ssnTo = -1;   // -1 = not used
        QString flareClass;   // "A".."X": a flare of that class in any period
    };
    QList<Row> rows(const Filter &f, int limit = 500);   // newest first
    int count();                                         // C#: table_count

    static constexpr int kSlotWidth = 300;   // C#: Truncate(..., 300)

    // tests only: a scratch database with the C# table layout (25-character columns)
    bool createTestDatabase();
    void dropTestDatabase();

private:
    bool exec(const QString &sql, const QVariantList &args);
    QString m_conn;
    DbSettings m_s;
    QString m_db;
    QString m_error;
};
