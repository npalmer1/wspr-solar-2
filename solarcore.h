#pragma once
// WSPR Solar 2 - the non-window part of the C# Solar form: reading NOAA's text and JSON files, and the
// rules that turn the numbers into levels (G/S/R scales, activity, conditions, radio bursts).
// Everything here is a plain function of its inputs (the time is passed in), so it can be tested.
//
// Differences from the C# version (also in PORTING_NOTES.md):
//  - The A/K indices: AK.txt over FTP as before (ftpget.h - Qt has no FTP of its own), then over https
//    from the same server; if neither works, daily-geomagnetic-indices.txt (planetary values only - no
//    Boulder line). findGeo reads either layout.
//  - Month names are always English ("Sep", not "Sept" as .NET writes with the en-GB culture, which
//    made the C# version miss the date every September).
//  - Bad or missing numbers give empty results instead of an exception that stopped the whole update
//    (e.g. a proton flux "0" with no "/" stopped the PF/flare grid filling).
//  - Radio-burst durations that cross midnight (2350-0005) are 15 minutes, not 1425.
//  - The highest R level of the period counts (C# kept the last M flare's level: M5 then M1 gave R1).
//  - Solar flux exactly 70 is "poor-fair" (C# gave an empty rating for 70).
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <array>

namespace Solar {

// ---- NOAA files ----------------------------------------------------------------------------------

inline const char *kNoaaRoot = "https://services.swpc.noaa.gov/";
inline const char *kGeoFtpUrl = "ftp://ftp.swpc.noaa.gov/pub/lists/geomag/AK.txt";   // C#: this
inline const char *kGeoUrl = "https://ftp.swpc.noaa.gov/pub/lists/geomag/AK.txt";
inline const char *kGeoUrlAlt = "https://services.swpc.noaa.gov/text/daily-geomagnetic-indices.txt";
inline const char *kSgasUrl = "https://services.swpc.noaa.gov/text/sgas.txt";
inline const char *kEventsUrl = "https://services.swpc.noaa.gov/text/solar-geophysical-event-reports.txt";
QString protonUrl(bool primary);   // GOES integral protons, 3 days, 5-minute values
inline const char *kFlaresUrl = "https://services.swpc.noaa.gov/json/goes/primary/xray-flares-7-day.json";
inline const char *kHamqslImage = "https://www.hamqsl.com/solar101vhf.php";
inline const char *kHamqslPage = "https://www.hamqsl.com/solar2.html";

// A index and the eight 3-hourly K indices of one station for one day (C#: SolarIndexes Ap, K00..K21)
struct Geo
{
    bool found = false;
    QString station;   // the station the values are from (Fredericksburg when Boulder isn't in the file)
    QString Ap;
    std::array<QString, 8> K;
};

// "2026 Nov 4" - the date as NOAA's geomagnetic files write it (C#: "yyyy MMM dd" without a leading 0)
QString geoDateKey(const QDate &d);

// C#: findGeo - the line for source ("Planetary" or "Boulder") under the date's heading in AK.txt, or
// the day's line in daily-geomagnetic-indices.txt (which has Fredericksburg, not Boulder: asked for
// Boulder, that gives Fredericksburg, with station saying so)
Geo findGeo(const QString &text, const QDate &day, const QString &source);

// daily-geomagnetic-indices.txt cut to its heading and the last `days` days (the 2-day view when
// AK.txt can't be had)
QString lastDays(const QString &dailyText, int days);

// Yesterday's solar flux, sunspot number and X-ray background from sgas.txt (C#: findSolar). Only when
// the report was issued today (UTC); found = false otherwise.
struct Daily
{
    bool found = false;
    QString flux, ssn, xray;
};
Daily findSolar(const QString &sgasText, const QDate &todayUtc);

// ---- levels (C#: findKplevel, find_activity_level, findSSNlevel, findIntensity, findS2, findR2) ----

QString kpLevel(const QString &kp);            // " (G1)" .. " (G5)", or "" (C#: findKplevel)
int kpG(const QString &kp);                     // 0..5
QString activityLevel(const QString &ap, int utcHour);   // "Quiet" .. "Severe storm"; "Unknown" before 03 UTC
QString ssnLevel(const QString &ssn);           // "Very low" .. "Very high", "--"
QString intensity(int sfu);                     // radio burst strength from solar flux units
QString protonS(const QString &pf);             // "avg/max" proton flux -> "-S1" .. "-S5" or ""
QString flareR(const QString &flares);          // "03:21/M1.2/0.01\n..." -> " (R1)" .. " (R5)" or ""
int levelDigit(const QString &cell, QChar scale);   // "12 (G2)" -> 2 for 'G'; 0 if none
QString zeros(const QString &v);                // "3.00" -> "3" (C#: zeros)
QString kpText(double kp);                      // a Kp value as the grids show it (C#: ToString("F2"), zeros)

// C#: findConditions / findStormLevel
QString hfRating(const QString &flux);          // "poor" .. "outstanding", "unknown"
QString stormLevel(const QString &activity, const QString &G, const QString &R, const QString &S, bool previous);
struct Conditions
{
    QString condition;   // conditionlabel
    QString storm;       // stormconditionlabel
};
// currentG/R/S: the last three hours (GClabel...); todayG/R/S: anything today (Glabel...)
Conditions conditions(const QString &flux, const QString &activity, const QString &currentG,
                      const QString &currentR, const QString &currentS, const QString &todayG,
                      const QString &todayR, const QString &todayS);

// ---- GOES protons and flares (C#: getProtonFlux, getSolarFlares) -----------------------------------

// The 3-hour period h1..h1+2:55 on `day`
struct PeriodResult
{
    QString text;        // C#: fluxdata / flaredata
    bool error = false;  // C#: satErr - try the other satellite
};
PeriodResult protonFlux(const QByteArray &json, const QDate &day, int h1);
PeriodResult flares(const QByteArray &json, const QDate &day, int h1);

// ---- radio bursts (C#: findBurst, findBurstTimeSlot, ParseBurstStorms, findcurrentBurst) -----------

// The bursts in today's event report, by 3-hour period: "RSP/0110-0122/III/1" lines joined by "\n".
// found = false when the report isn't today's (C# then kept whatever it had before).
struct BurstSlots
{
    bool found = false;
    std::array<QString, 8> slot;
};
BurstSlots findBursts(const QString &eventsText, const QDate &todayUtc);

// One period's bursts summarised (C#: BurstData, one column of it)
struct BurstPeriod
{
    QString RSPlevel, RSPband, RSPspan, rbrSpan, rnsSpan, rbrLevel, rnsLevel;
};
BurstPeriod parseBurstPeriod(const QString &cell);
QString burstCellText(const BurstPeriod &b);   // C#: the BurstdataGridView cell
int periodOfHour(int utcHour);                 // 0..7 (C#: findHour)
int maxBurstLevel(const BurstPeriod &b);       // 0..5 (C#: findMaxBurst)
QString previousBurst(const std::array<BurstPeriod, 8> &bd, int period);   // C#: findPreviousBurst

struct BurstNow
{
    QString warning;   // Burstwarninglabel
    QString current;   // currentBurstlabel
};
BurstNow currentBurst(const std::array<BurstPeriod, 8> &bd, int period);   // C#: findcurrentBurst

// The period whose values are shown as "last 3h" (C#: find_current_G / _S / _R: h = ((hour-3)/3)*3)
// G returns -1 before 03 UTC (C# showed "--"); S and R use period 0
int lastPeriod(int utcHour, bool forG);

} // namespace Solar
