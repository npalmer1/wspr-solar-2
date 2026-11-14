// WSPR Solar 2 - see solarcore.h. Function names in the comments are the C# ones in Solar.cs.
#include "solarcore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

#include <cmath>

namespace Solar {

namespace {

const QRegularExpression kSpace(QStringLiteral("\\s+"));

QStringList tokens(const QString &line)
{
    return line.split(kSpace, Qt::SkipEmptyParts);
}

bool isNumber(const QString &s)
{
    bool ok = false;
    s.toDouble(&ok);
    return ok;
}

int monthNumber(const QString &abbrev)
{
    static const QStringList m{"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    return int(m.indexOf(abbrev.left(3).toLower())) + 1;   // 0 if unknown
}

// "2026 Nov 14" (three tokens from position i) -> date
QDate dateFromTokens(const QStringList &t, int i)
{
    if (t.size() < i + 3)
        return {};
    const int m = monthNumber(t[i + 1]);
    return m > 0 ? QDate(t[i].toInt(), m, t[i + 2].toInt()) : QDate();
}

// C#: Math.Round(x, 4).ToString() - shortest form, no trailing zeros
QString round4(double v)
{
    const double r = std::round(v * 10000.0) / 10000.0;
    return QString::number(r == 0.0 ? 0.0 : r, 'g', 15);
}

QDateTime utcTime(const QString &iso)
{
    QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
    if (t.isValid() && t.timeSpec() == Qt::LocalTime)
        t.setTimeZone(QTimeZone::utc());   // NOAA's times are UTC with or without the Z
    return t.toUTC();
}

// A 3-hour period of `day`: h1:00 to h1+2:55 (C#: dt1 / dt2 in getProtonFlux / getSolarFlares)
bool inPeriod(const QDateTime &t, const QDate &day, int h1)
{
    const QDateTime from(day, QTime(h1, 0), QTimeZone::utc());
    const QDateTime to(day, QTime(h1 + 2, 55), QTimeZone::utc());
    return t >= from && t <= to;
}

QString levelName(int level)   // the names C# used for radio bursts
{
    switch (level) {
    case 1: return "weak";
    case 2: return "moderate";
    case 3: return "strong";
    case 4: return "very strong";
    case 5: return "extreme";
    default: return "none";
    }
}

int burstWordLevel(const QString &level)   // RBR / RNS intensity words (C#: findcurrentBurst)
{
    const QString l = level.toLower();
    if (l.contains("very low"))
        return 0;
    if (l.contains("low"))
        return 1;
    if (l.contains("moderate"))
        return 2;
    if (l.contains("very high"))   // before "high" (C# tested "high" first for RBR, so very high counted as 3)
        return 4;
    if (l.contains("high"))
        return 3;
    if (l.contains("extreme"))
        return 5;
    return 0;
}

int sweepLevel(const QString &rspLevel)   // C#: Weak/Moderate/Strong (Severe was not counted)
{
    const QString r = rspLevel.toLower();
    if (r.contains("weak"))
        return 1;
    if (r.contains("moderate"))
        return 2;
    if (r.contains("strong"))
        return 3;
    if (r.contains("severe"))
        return 4;
    if (r.contains("extreme"))
        return 5;
    return 0;
}

} // namespace

QString protonUrl(bool primary)
{
    return QString("https://services.swpc.noaa.gov/json/goes/%1/integral-protons-3-day.json")
        .arg(primary ? "primary" : "secondary");
}

QString geoDateKey(const QDate &d)
{
    return QLocale::c().toString(d, "yyyy MMM d");
}

Geo findGeo(const QString &text, const QDate &day, const QString &source)
{
    Geo g;
    const QStringList lines = text.split('\n');
    static const QRegularExpression akDate("^\\s*(\\d{4})\\s+([A-Za-z]{3})\\s+(\\d{1,2})\\b");
    static const QRegularExpression dailyDate("^\\s*(\\d{4})\\s+(\\d{1,2})\\s+(\\d{1,2})\\s");
    auto take = [&g](const QStringList &t) {   // the A index and the eight K indices end the line
        if (t.size() < 9)
            return;
        const QStringList last = t.mid(t.size() - 9);
        for (const QString &v : last)
            if (!isNumber(v))
                return;
        g.Ap = last[0];
        for (int i = 0; i < 8; ++i)
            g.K[i] = last[i + 1];
        g.found = true;
    };

    if (text.contains("Planetary(")) {
        // AK.txt: a "2026 Nov 14" line, then one line per station
        bool inDay = false;
        for (const QString &line : lines) {
            const auto m = akDate.match(line);
            if (m.hasMatch()) {
                const int mon = monthNumber(m.captured(2));
                inDay = mon > 0 && QDate(m.captured(1).toInt(), mon, m.captured(3).toInt()) == day;
                if (!inDay)
                    continue;
            }
            if (inDay && line.contains(source)) {
                take(tokens(line));
                g.station = source;
                return g;
            }
        }
        return g;
    }
    // daily-geomagnetic-indices.txt: one line a day - the date, then A and eight K indices for
    // Fredericksburg, College and Planetary. Missing values run together ("1-1-1"), so split those first.
    static const QRegularExpression glued("(?<=\\d)-");
    for (const QString &line : lines) {
        const auto m = dailyDate.match(line);
        if (!m.hasMatch() || QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()) != day)
            continue;
        QString l = line;
        const QStringList t = tokens(l.replace(glued, " -"));
        if (t.size() < 3 + 27)
            return g;
        if (source == "Planetary") {
            take(t.mid(3 + 18, 9));
            g.station = "Planetary";
        } else {
            take(t.mid(3, 9));
            g.station = "Fredericksburg";
        }
        return g;
    }
    return g;
}

QString lastDays(const QString &dailyText, int days)
{
    static const QRegularExpression dateLine("^\\s*\\d{4}\\s+\\d{1,2}\\s+\\d{1,2}\\s");
    QStringList head, data;
    for (QString line : dailyText.split('\n')) {
        line.remove('\r');
        if (dateLine.match(line).hasMatch())
            data << line;
        else if (data.isEmpty() && !line.trimmed().isEmpty())
            head << line;
    }
    return (head + data.mid(qMax(0, int(data.size()) - days))).join('\n') + '\n';
}

Daily findSolar(const QString &sgasText, const QDate &todayUtc)
{
    Daily d;
    const int issued = sgasText.indexOf(":Issued:");
    if (issued < 0 || !sgasText.contains("Daily Indices:"))
        return d;
    const QString rest = sgasText.mid(issued);
    const QStringList lines = rest.split('\n');
    if (dateFromTokens(tokens(lines.value(0)), 1) != todayUtc)   // ":Issued: 2026 Nov 14 0030 UTC"
        return d;
    QString xray;
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const int x = line.indexOf("X-ray Background");
        if (x >= 0)
            xray = line.mid(x + int(strlen("X-ray Background"))).trimmed();
        if (line.contains("10 cm")) {   // "10 cm 151  SSN 128  Afr/Ap 006/005   X-ray Background C1.0"
            const QStringList t = tokens(line);
            if (t.size() > 4) {
                d.flux = t[2];
                d.ssn = t[4];
                d.found = true;
            }
            break;
        }
    }
    if (!xray.isEmpty())
        d.xray = xray;
    return d;
}

// ---- levels -------------------------------------------------------------------------------------

int kpG(const QString &kp)
{
    bool ok = false;
    const double k = kp.trimmed().toDouble(&ok);
    if (!ok)
        return 0;
    if (k >= 9)
        return 5;
    if (k >= 8)
        return 4;
    if (k >= 7)
        return 3;
    if (k >= 6)
        return 2;
    if (k >= 5)
        return 1;
    return 0;
}

QString kpLevel(const QString &kp)
{
    const int g = kpG(kp);
    return g > 0 ? QString(" (G%1)").arg(g) : QString();
}

QString activityLevel(const QString &ap, int utcHour)
{
    bool ok = false;
    const double a = ap.trimmed().toDouble(&ok);
    if (!ok || utcHour < 3)   // C#: today's A index isn't meaningful until the first K index is in
        return "Unknown";
    if (a >= 0 && a <= 7)
        return "Quiet";
    if (a >= 8 && a <= 15)
        return "Unsettled";
    if (a >= 16 && a <= 29)
        return "Active";
    if (a >= 30 && a <= 49)
        return "Minor storm";
    if (a >= 50 && a <= 99)
        return "Major storm";
    if (a >= 100)
        return "Severe storm";
    return "Unknown";
}

QString ssnLevel(const QString &ssn)
{
    bool ok = false;
    const int s = ssn.trimmed().toInt(&ok);
    if (!ok || s < 0)
        return "--";
    if (s <= 19)
        return "Very low";
    if (s <= 49)
        return "Low";
    if (s <= 99)
        return "Medium";
    if (s <= 149)
        return "High";
    return "Very high";   // C#: "> 150", so exactly 150 showed "--"
}

QString intensity(int sfu)
{
    if (sfu < 50)
        return "Quiet";
    if (sfu < 200)
        return "Low";
    if (sfu < 500)
        return "Moderate";
    if (sfu < 1000)
        return "High";
    if (sfu < 10000)
        return "Very high";
    return "Extreme";
}

QString protonS(const QString &pf)
{
    const QStringList p = pf.trimmed().split('/');
    if (p.size() < 2)
        return {};   // C#: an exception for "0" (no "/"), which stopped the whole grid
    const double t = p[1].toDouble();
    int s = 0;
    if (t >= 100000)
        s = 5;
    else if (t >= 10000)
        s = 4;
    else if (t >= 1000)
        s = 3;
    else if (t >= 100)
        s = 2;
    else if (t >= 10)
        s = 1;
    return s > 0 ? QString("-S%1").arg(s) : QString();
}

QString flareR(const QString &flares)
{
    int r = 0;
    for (const QString &part : flares.split(QRegularExpression("[/\\r\\n]"), Qt::SkipEmptyParts)) {
        const QChar c = part.at(0).toUpper();
        bool ok = false;
        const double t = part.mid(1).toDouble(&ok);
        if (!ok || t <= 0)
            continue;
        int level = 0;
        if (c == 'M')
            level = t < 5 ? 1 : 2;
        else if (c == 'X')
            level = t < 10 ? 3 : (t < 20 ? 4 : 5);
        r = qMax(r, level);
    }
    return r > 0 ? QString(" (R%1)").arg(r) : QString();
}

int levelDigit(const QString &cell, QChar scale)
{
    static const QRegularExpression any("([GRS])(\\d)");
    int best = 0;
    auto it = any.globalMatch(cell);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.captured(1).at(0) == scale)
            best = qMax(best, m.captured(2).toInt());
    }
    return best;
}

QString zeros(const QString &v)
{
    const QStringList c = v.split('.');
    if (c.size() > 1 && (c[1] == "0" || c[1] == "00"))
        return c[0];
    return v;
}

QString kpText(double kp)
{
    return zeros(QString::number(kp, 'f', 2));
}

// ---- conditions ----------------------------------------------------------------------------------

QString hfRating(const QString &flux)
{
    if (flux.trimmed().isEmpty())
        return "unknown";
    const int t = int(flux.trimmed().toDouble());
    if (t > 199)
        return "outstanding";
    if (t > 149)
        return "very good";
    if (t > 129)
        return "good";
    if (t > 99)
        return "fair";
    if (t >= 70)
        return "poor-fair";
    return "poor";
}

QString stormLevel(const QString &activity, const QString &G, const QString &R, const QString &S, bool previous)
{
    // NOAA scale levels in the labels ("G1", "R3"...): the highest counts (C# took the first of 1..5 found)
    int level = 0;
    for (const QString &x : {G, R, S})
        for (int i = 1; i <= 5; ++i)
            if (x.contains(QString::number(i)))
                level = qMax(level, i);
    // Ap storm words: minor = 1 (minor), major = strong, severe = severe (as C#)
    const QString a = activity.toLower();
    if (a.contains("severe"))
        level = qMax(level, 4);
    else if (a.contains("major"))
        level = qMax(level, 3);
    else if (a.contains("minor"))
        level = qMax(level, 1);

    switch (level) {
    case 1:
        return previous ? "Likely normal, but minor storm or blackout reported earlier"
                        : "Possibly slightly unstable/degraded (minor storm or blackout)";
    case 2:
        return previous ? "Likely normal, but moderate storm or blackout reported earlier"
                        : "Possibly moderately unstable/degraded (moderate storm or blackout)";
    case 3:
        return previous ? "Likely returning to normal (strong storm or blackout reported earlier)"
                        : "Possibly quite unstable/degraded (strong storm or blackout)";
    case 4:
        return previous ? "Likely returning to normal (severe storm or blackout reported earlier)"
                        : "Likely severely unstable/degraded (severe storm or blackout)";
    case 5:
        return previous ? "Likely normal/disturbed, extreme storm or blackout reported earlier"
                        : "Likely extremely unstable/degraded (extreme storm or blackout)";
    default:
        return {};
    }
}

Conditions conditions(const QString &flux, const QString &activity, const QString &currentG,
                      const QString &currentR, const QString &currentS, const QString &todayG,
                      const QString &todayR, const QString &todayS)
{
    Conditions c;
    QString p = hfRating(flux);
    const QString act = activity.toLower();
    p += act.contains("storm") ? " ...but " + act : " ...Sun: " + act;
    if (currentG.contains('G') || currentR.contains('R') || currentS.contains('S'))
        c.storm = stormLevel(act, currentG, currentR, currentS, false);
    else if (act.contains("storm"))
        c.storm = "Possibly unstable/degraded (" + act + ")";
    else if (todayG.contains('G') || todayR.contains('R') || todayS.contains('S'))
        c.storm = stormLevel(act, todayG, todayR, todayS, true);
    else
        c.storm = "Likely normal - (no storm or blackout)";
    c.condition = "SFI: " + flux + " - Higher HF propagation: " + p;
    return c;
}

// ---- GOES ----------------------------------------------------------------------------------------

PeriodResult protonFlux(const QByteArray &json, const QDate &day, int h1)
{
    PeriodResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        r.error = true;
        return r;
    }
    double fluxmax = 0, total = 0;
    int count = 0;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("energy").toString() != ">=10 MeV")
            continue;
        if (!inPeriod(utcTime(o.value("time_tag").toString()), day, h1))
            continue;
        const QJsonValue fv = o.value("flux");
        const double f = fv.isString() ? fv.toString().toDouble() : fv.toDouble();
        // C#: the maximum only moves after the value has gone above it 3 times (2 x 5-minute samples),
        // so a single glitch doesn't count
        if (f > fluxmax)
            ++count;
        if (f > fluxmax && count > 2) {
            fluxmax = f;
            count = 0;
        }
        total += f;
    }
    const double avg = total / 35;   // C#: "average of 35 five min samples"
    if (avg > 0 && total > 0)
        r.text = round4(avg) + "/" + round4(fluxmax);
    return r;
}

PeriodResult flares(const QByteArray &json, const QDate &day, int h1)
{
    PeriodResult r;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        r.error = true;
        return r;
    }
    QStringList out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QDateTime maxt = utcTime(o.value("max_time").toString());
        if (!maxt.isValid() || !inPeriod(maxt, day, h1))
            continue;
        const QJsonValue xr = o.value("current_int_xrlong");   // can be null (C#: an exception ended the list)
        const double intXR = xr.isString() ? xr.toString().toDouble() : xr.toDouble();
        out << maxt.toString("HH:mm") + "/" + o.value("max_class").toString() + "/" + round4(intXR);
    }
    r.text = out.join('\n');
    return r;
}

// ---- radio bursts --------------------------------------------------------------------------------

int periodOfHour(int utcHour)
{
    return qBound(0, utcHour / 3, 7);
}

int lastPeriod(int utcHour, bool forG)
{
    if (utcHour < 3)
        return forG ? -1 : 0;
    return (utcHour - 3) / 3;
}

BurstSlots findBursts(const QString &eventsText, const QDate &todayUtc)
{
    BurstSlots b;
    bool today = false;
    for (QString line : eventsText.split('\n')) {
        line.remove('\r');
        if (line.contains("Created:")) {   // ":Created: 2026 Nov 14 2132 UT"
            today = dateFromTokens(tokens(line), 1) == todayUtc;
            if (!today)
                return b;   // not today's report (C# returned here too)
            b.found = true;
            continue;
        }
        if (!today || line.startsWith('#'))
            continue;
        const QStringList s = tokens(line);
        if (s.size() <= 8)
            continue;
        const int i = s[1] == "+" ? 1 : 0;   // "+" marks an event that is part of a larger one
        if (s.size() <= 8 + i)
            continue;
        const QString type = s[6 + i];
        if (type != "RBR" && type != "RSP" && type != "RNS")
            continue;
        QString start = s[1 + i], end;
        if (start.contains("///"))
            start = "?";
        else
            end = s[3 + i].contains("///") ? "?" : s[3 + i];
        const QString part = s[8 + i];
        const QString entry = type + "/" + (end.isEmpty() ? start : start + "-" + end) + "/" + part;
        // the period of the start time (of the end time if the start isn't known)
        const QString when = start != "?" ? start : end;
        const QTime t = QTime::fromString(when, "HHmm");
        if (!t.isValid())
            continue;   // C# put these in 00-03
        QString &slot = b.slot[periodOfHour(t.hour())];
        slot += (slot.isEmpty() ? "" : "\n") + entry;
    }
    return b;
}

BurstPeriod parseBurstPeriod(const QString &cell)
{
    BurstPeriod bp;
    int level = 0, rspDur = 0, rbrDur = 0, rnsDur = 0, rbrLevel = 0, rnsLevel = 0;
    QString lstr, band;
    for (QString line : cell.split('\n')) {
        line.remove('\r');
        line = line.trimmed();
        const bool rsp = line.startsWith("RSP"), rbr = line.startsWith("RBR"), rns = line.startsWith("RNS");
        if (!rsp && !rbr && !rns)
            continue;   // C# stopped reading the period here, leaving the last period's values
        const QStringList s = line.split('/');
        int dur = 1;
        if (s.size() > 1) {
            const QStringList t = s[1].split('-');
            if (t.size() > 1 && t[0] != "?" && t[1] != "?" && !t[0].isEmpty() && !t[1].isEmpty()) {
                const QTime from = QTime::fromString(t[0], "HHmm"), to = QTime::fromString(t[1], "HHmm");
                if (from.isValid() && to.isValid()) {
                    int m = from.secsTo(to) / 60;
                    if (m < 0)
                        m += 24 * 60;   // past midnight (C# took the absolute difference: 1425 for 2350-0005)
                    dur = qMax(1, m);
                }
            }
        }
        if (rsp) {   // sweep: the class after the last "/" (1..5) is the strength, the type the band
            static const char *names[] = {"", "Weak", "Moderate", "Strong", "Severe", "Extreme"};
            for (int k = 1; k <= 5; ++k)
                if (line.endsWith(QString("/%1").arg(k))) {
                    rspDur += dur;
                    if (dur > 3 && level < k) {
                        lstr = names[k];
                        level = k;
                    }
                }
            // III and VI wideband (VLF to 1 GHz), II and V 25-200 MHz, I VHF; CTM wideband (as C#)
            QChar b;
            if (line.contains("III") || line.contains("VI"))
                b = 'W';
            else if (line.contains("II") || line.contains("V"))
                b = 'T';
            else if (line.contains("I"))
                b = 'V';
            else if (line.contains("CTM"))
                b = 'W';
            if (!b.isNull() && (dur > 3 || rspDur > 10) && !band.contains(b))
                band += b;
        } else {   // RBR short fixed-frequency burst, RNS longer noise storm: strength in solar flux units
            const int sfu = s.size() > 2 ? s[2].toInt() : 0;
            if (rbr) {
                rbrDur += dur;
                rbrLevel = qMax(rbrLevel, sfu);
            } else {
                rnsDur += dur;
                rnsLevel = qMax(rnsLevel, sfu);
            }
        }
    }
    if (lstr.isEmpty())
        lstr = rspDur > 0 ? "insignificant" : "none";
    bp.RSPlevel = lstr;
    if (band.contains('W'))
        bp.RSPband = "Wideband";
    else if (band.contains('G'))
        bp.RSPband = "25MHz-2GHz";
    else if (band.contains('T'))
        bp.RSPband = "25-200MHz";
    else if (band.contains('V'))
        bp.RSPband = "VHF";
    bp.RSPspan = rspDur > 5 ? QString::number(rspDur) : QString();
    if (rbrDur > 0) {
        bp.rbrSpan = QString::number(rbrDur);
        bp.rbrLevel = rbrLevel > 0 ? intensity(rbrLevel) : QString();
    } else {
        bp.rbrLevel = "none";
    }
    if (rnsDur > 0) {
        bp.rnsSpan = QString::number(rnsDur);
        bp.rnsLevel = rnsLevel > 0 ? intensity(rnsLevel) : QString();
    } else {
        bp.rnsLevel = "none";
    }
    return bp;
}

QString burstCellText(const BurstPeriod &b)
{
    QString t;
    if (b.RSPspan.isEmpty())
        t = b.RSPlevel + "\n\n";
    else
        t = b.RSPspan + "min " + b.RSPlevel + "\n" + b.RSPband + "\n";
    t += b.rbrSpan + (b.rbrSpan.isEmpty() ? "" : "min ") + b.rbrLevel + "\n";
    t += b.rnsSpan + (b.rnsSpan.isEmpty() ? "" : "min ") + b.rnsLevel;
    return t;
}

int maxBurstLevel(const BurstPeriod &b)
{
    return qMax(sweepLevel(b.RSPlevel), qMax(burstWordLevel(b.rbrLevel), burstWordLevel(b.rnsLevel)));
}

QString previousBurst(const std::array<BurstPeriod, 8> &bd, int period)
{
    int maxLevel = 0;
    for (int i = 0; i < period && i < 8; ++i)
        maxLevel = qMax(maxLevel, maxBurstLevel(bd[i]));
    return maxLevel > 0 ? levelName(maxLevel) : QString();
}

BurstNow currentBurst(const std::array<BurstPeriod, 8> &bd, int period)
{
    BurstNow n;
    if (period < 0 || period > 7)
        return n;
    const BurstPeriod &b = bd[period];
    const int rsp = sweepLevel(b.RSPlevel);
    const int rbr = burstWordLevel(b.rbrLevel);
    const int rns = burstWordLevel(b.rnsLevel);
    const int total = qMax(rsp, qMax(rbr, rns));
    const QString rlevel = levelName(total);
    const QString band = b.RSPband.toLower().contains("wideband") ? QString("wideband") : b.RSPband;

    QString r, oo;
    if (rsp > 0) {
        r = rlevel + " " + band + " radio sweep";
        oo = "swept radio burst";
    }
    if (rbr > 0 && rbr > rns)
        oo = "short " + rlevel + " radio burst(s)";
    if (rns > 0 && rns >= rbr) {
        if (r.contains("radio sweep"))
            r = " and " + r;
        oo = "longer " + rlevel + " radio noise storm(s)" + r;
    }
    oo = "Reception may be affected by " + oo;
    if (rsp == 0 && rbr == 0 && rns == 0)
        oo = "No significant radio bursts at present";
    const QString p = previousBurst(bd, period);
    if (!p.isEmpty() && oo.contains("No significant"))
        oo += ", but " + p + " bursts reported earlier";
    n.warning = oo;
    n.current = total == 0 ? QString("none or no significant bursts") : "some " + rlevel + " bursts";
    return n;
}

} // namespace Solar
