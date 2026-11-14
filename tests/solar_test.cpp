// WSPR Solar 2 core: NOAA files (real ones saved on 14 Nov 2026, in tests/data), levels, radio bursts.
// The wspr_sol database part is tested on a SCRATCH database wspr_sol_test on a local MySQL (never the
// real wspr_sol; skipped if there's no MySQL).
#include "solarcore.h"
#include "solarstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
                         else std::printf("ok   %s\n", #cond); } while (0)

static QByteArray data(const QString &name)
{
    QFile f(QString(SOLAR_TEST_DATA) + "/" + name);
    if (!f.open(QIODevice::ReadOnly))
        std::printf("missing test file %s\n", qPrintable(name));
    return f.readAll();
}

static void testGeo()
{
    const QString ak = QString::fromLatin1(data("AK.txt"));
    const QDate d14(2026, 11, 14), d13(2026, 11, 13);
    CHECK(Solar::geoDateKey(QDate(2026, 9, 4)) == "2026 Sep 4");   // not "Sept", not "04"

    Solar::Geo p = Solar::findGeo(ak, d14, "Planetary");
    CHECK(p.found && p.Ap == "4" && p.K[0] == "0.33" && p.K[5] == "1.00" && p.K[7] == "-1.00");
    Solar::Geo b = Solar::findGeo(ak, d14, "Boulder");
    CHECK(b.found && b.station == "Boulder" && b.Ap == "-1" && b.K[5] == "3" && b.K[6] == "-1");
    Solar::Geo y = Solar::findGeo(ak, d13, "Planetary");
    CHECK(y.found && y.Ap == "4" && y.K[0] == "1.33");
    CHECK(!Solar::findGeo(ak, QDate(2026, 11, 15), "Planetary").found);
    CHECK(!Solar::findGeo(ak, QDate(2026, 11, 1), "Planetary").found);   // "2026 Nov 1" isn't "Nov 13/14"

    const QString daily = QString::fromLatin1(data("daily-geomagnetic-indices.txt"));
    Solar::Geo dp = Solar::findGeo(daily, d14, "Planetary");
    CHECK(dp.found && dp.Ap == "4" && dp.K[0] == "0.33" && dp.K[6] == "-1.00");
    Solar::Geo fb = Solar::findGeo(daily, d14, "Boulder");   // no Boulder in it: Fredericksburg
    CHECK(fb.found && fb.station == "Fredericksburg" && fb.Ap == "-1" && fb.K[4] == "2" && fb.K[6] == "-1");
    Solar::Geo n3 = Solar::findGeo(daily, QDate(2026, 11, 3), "Boulder");   // "-1-1-1-1-1-1-1-1"
    CHECK(n3.found && n3.Ap == "-1" && n3.K[7] == "-1");
    CHECK(Solar::findGeo(daily, QDate(2026, 11, 3), "Planetary").Ap == "5");
    CHECK(Solar::findGeo(daily, QDate(2026, 10, 31), "Planetary").K[6] == "6.00");
}

static void testSgas()
{
    const QString s = QString::fromLatin1(data("sgas.txt"));
    Solar::Daily d = Solar::findSolar(s, QDate(2026, 11, 14));
    CHECK(d.found && d.flux == "119" && d.ssn == "118" && d.xray == "B6.7");
    CHECK(!Solar::findSolar(s, QDate(2026, 11, 15)).found);   // yesterday's report: not used (as C#)
}

static void testLevels()
{
    CHECK(Solar::kpLevel("4.67").isEmpty() && Solar::kpLevel("5") == " (G1)" && Solar::kpLevel("6.00") == " (G2)");
    CHECK(Solar::kpLevel("8.33") == " (G4)" && Solar::kpLevel("9") == " (G5)" && Solar::kpLevel("-1").isEmpty());
    CHECK(Solar::kpLevel("").isEmpty());   // C#: an exception
    CHECK(Solar::activityLevel("4", 12) == "Quiet" && Solar::activityLevel("12", 12) == "Unsettled");
    CHECK(Solar::activityLevel("35", 12) == "Minor storm" && Solar::activityLevel("120", 12) == "Severe storm");
    CHECK(Solar::activityLevel("35", 2) == "Unknown" && Solar::activityLevel("-1", 12) == "Unknown");
    CHECK(Solar::ssnLevel("118") == "High" && Solar::ssnLevel("150") == "Very high" && Solar::ssnLevel("") == "--");
    CHECK(Solar::intensity(30) == "Quiet" && Solar::intensity(120) == "Low" && Solar::intensity(600) == "High");
    CHECK(Solar::protonS("0.2379/0.2741").isEmpty() && Solar::protonS("5/12") == "-S1");
    CHECK(Solar::protonS("50/150000") == "-S5" && Solar::protonS("0").isEmpty() && Solar::protonS("").isEmpty());
    CHECK(Solar::flareR("00:10/C1.4/0.0013\n01:41/C1.7/0.0026").isEmpty());
    CHECK(Solar::flareR("07:46/M1.7/0.0119") == " (R1)");
    CHECK(Solar::flareR("07:46/M5.7/0.0119\n08:00/M1.0/0.01") == " (R2)");   // highest, not last (C#: R1)
    CHECK(Solar::flareR("07:46/X12/0.5\n08:00/M1.0/0.01") == " (R4)");
    CHECK(Solar::levelDigit("1.33 (G2)", 'G') == 2 && Solar::levelDigit("0.2/12-S1", 'S') == 1);
    CHECK(Solar::levelDigit("07:46/M1.7/0.01 (R1)", 'R') == 1 && Solar::levelDigit("--", 'G') == 0);
    CHECK(Solar::zeros("3.00") == "3" && Solar::zeros("3.33") == "3.33" && Solar::zeros("12") == "12");
    CHECK(Solar::kpText(1.3300000429153442) == "1.33" && Solar::kpText(2.0) == "2");
    CHECK(Solar::hfRating("119") == "fair" && Solar::hfRating("70") == "poor-fair" && Solar::hfRating("69") == "poor");
    CHECK(Solar::hfRating("200") == "outstanding" && Solar::hfRating("") == "unknown");
}

static void testConditions()
{
    Solar::Conditions c = Solar::conditions("119", "Quiet", "--", "--", "--", "--", "--", "--");
    CHECK(c.condition == "SFI: 119 - Higher HF propagation: fair ...Sun: quiet");
    CHECK(c.storm == "Likely normal - (no storm or blackout)");
    c = Solar::conditions("150", "Minor storm", "--", "--", "--", "--", "--", "--");
    CHECK(c.condition == "SFI: 150 - Higher HF propagation: very good ...but minor storm");
    CHECK(c.storm == "Possibly unstable/degraded (minor storm)");
    c = Solar::conditions("150", "Active", "G2", "--", "--", "G2", "--", "--");
    CHECK(c.storm == "Possibly moderately unstable/degraded (moderate storm or blackout)");
    c = Solar::conditions("150", "Active", "--", "--", "--", "--", "R3", "--");
    CHECK(c.storm == "Likely returning to normal (strong storm or blackout reported earlier)");
    // the highest level counts: G1 now with an R3 blackout (C# reported "minor")
    CHECK(Solar::stormLevel("active", "G1", "R3", "--", false).contains("strong"));
}

static void testGoes()
{
    const QByteArray p = data("integral-protons-3-day.json");
    const QDate d14(2026, 11, 14);
    // expected values worked out the C# way (count/max rule, average over 35 samples)
    CHECK(Solar::protonFlux(p, d14, 0).text == "0.2379/0.2741");
    CHECK(Solar::protonFlux(p, d14, 3).text == "0.2386/0.2491");
    CHECK(Solar::protonFlux(p, d14, 18).text == "0.2147/0.2919");
    CHECK(Solar::protonFlux(p, d14, 21).text.isEmpty() && !Solar::protonFlux(p, d14, 21).error);   // not yet
    CHECK(Solar::protonFlux(p, QDate(2026, 11, 13), 0).text == "0.2407/0.2514");
    CHECK(Solar::protonFlux("<html>", d14, 0).error);   // not JSON: try the other satellite

    const QByteArray f = data("xray-flares-7-day.json");
    CHECK(Solar::flares(f, d14, 0).text == "00:10/C1.4/0.0013\n01:41/C1.7/0.0026");
    CHECK(Solar::flares(f, d14, 18).text == "18:45/C2.2/0.0014");
    CHECK(Solar::flares(f, d14, 9).text.isEmpty());
    CHECK(Solar::flares(f, QDate(2026, 11, 8), 6).text == "06:04/C2.3/0.0012\n06:36/C2.9/0.0028\n07:46/M1.7/0.0119\n08:43/C8.0/0.004");
    CHECK(Solar::flareR(Solar::flares(f, QDate(2026, 11, 8), 6).text) == " (R1)");
}

static void testBursts()
{
    const QString ev = QString::fromLatin1(data("solar-geophysical-event-reports.txt"));
    Solar::BurstSlots s = Solar::findBursts(ev, QDate(2026, 11, 14));
    CHECK(s.found);
    CHECK(s.slot[2] == "RSP/0804-0811/III/2\nRSP/0848-0852/III/2\nRSP/0848-0852/III/2");
    CHECK(s.slot[3] == "RSP/0928-0931/III/2\nRSP/1030-1031/III/1");
    CHECK(s.slot[0].isEmpty() && s.slot[7].isEmpty());   // the XRA (flare) lines aren't bursts
    CHECK(!Solar::findBursts(ev, QDate(2026, 11, 15)).found);

    std::array<Solar::BurstPeriod, 8> bd;
    for (int i = 0; i < 8; ++i)
        bd[i] = Solar::parseBurstPeriod(s.slot[i]);
    CHECK(bd[2].RSPlevel == "Moderate" && bd[2].RSPband == "Wideband" && bd[2].RSPspan == "15");
    CHECK(Solar::burstCellText(bd[2]) == "15min Moderate\nWideband\nnone\nnone");
    CHECK(bd[3].RSPlevel == "insignificant" && bd[3].RSPspan.isEmpty() && bd[3].RSPband.isEmpty());
    CHECK(bd[0].RSPlevel == "none" && bd[0].rbrLevel == "none");
    CHECK(Solar::burstCellText(bd[3]) == "insignificant\n\nnone\nnone");

    Solar::BurstNow n = Solar::currentBurst(bd, 3);   // 09-12 UTC
    CHECK(n.warning == "No significant radio bursts at present, but moderate bursts reported earlier");
    CHECK(n.current == "none or no significant bursts");
    n = Solar::currentBurst(bd, 2);
    CHECK(n.warning == "Reception may be affected by swept radio burst" && n.current == "some moderate bursts");

    // short bursts and noise storms; a duration past midnight
    Solar::BurstPeriod rb = Solar::parseBurstPeriod("RBR/0256-0256/120\nRNS/2350-0005/600\nRSP/2350-0005/II/3");
    CHECK(rb.rbrSpan == "1" && rb.rbrLevel == "Low" && rb.rnsSpan == "15" && rb.rnsLevel == "High");
    CHECK(rb.RSPlevel == "Strong" && rb.RSPband == "25-200MHz" && rb.RSPspan == "15");
    std::array<Solar::BurstPeriod, 8> one{};
    for (auto &p : one)
        p = Solar::parseBurstPeriod({});
    one[7] = rb;
    n = Solar::currentBurst(one, 7);
    CHECK(n.warning == "Reception may be affected by longer strong radio noise storm(s) and strong 25-200MHz radio sweep");
    CHECK(Solar::periodOfHour(0) == 0 && Solar::periodOfHour(23) == 7);
    CHECK(Solar::lastPeriod(2, true) == -1 && Solar::lastPeriod(2, false) == 0 && Solar::lastPeriod(10, true) == 2);
}

static void testStore()
{
    SolarStore st(DbSettings{"127.0.0.1", 3306, "admin", "wspr"}, "wspr_sol_test");
    if (!st.createTestDatabase()) {
        std::printf("no scratch MySQL - database tests skipped (%s)\n", qPrintable(st.lastError()));
        return;
    }
    CHECK(st.open());
    CHECK(st.widenColumns());   // pf/fl/s columns from 25 to 300 characters
    const QDate d14(2026, 11, 14), d13(2026, 11, 13);
    CHECK(st.saveDefault(d14));
    CHECK(st.saveDefault(d14));   // already there: fine (C#: an error it ignored)
    Solar::Geo g;
    g.found = true;
    g.Ap = "4";
    g.K = {"0.33", "0.67", "1.00", "1.33", "1.67", "1.00", "-1.00", "-1.00"};
    CHECK(st.saveGeo(d14, g));
    CHECK(st.saveGeo(d13, g));
    Solar::Daily dy;
    dy.found = true;
    dy.flux = "119";
    dy.ssn = "118";
    dy.xray = "B6.7";
    CHECK(st.saveSolar(d13, dy));
    std::array<QString, 8> pf{"0.2379/0.2741", "0.2386/0.2491", "", "", "", "", "0.2147/0.2919", ""};
    CHECK(st.saveSlots(d14, SolarStore::Column::ProtonFlux, pf));
    std::array<QString, 8> fl{"00:10/C1.4/0.0013\n01:41/C1.7/0.0026", "", "", "", "", "", "18:45/C2.2/0.0014", ""};
    CHECK(st.saveSlots(d14, SolarStore::Column::Flares, fl));
    std::array<QString, 8> bs;
    bs[2] = "RSP/0804-0811/III/2\nRSP/0848-0852/III/2\nRSP/0848-0852/III/2";   // 59 characters
    CHECK(st.saveSlots(d14, SolarStore::Column::Bursts, bs));

    const QList<SolarStore::Row> rows = st.rows(SolarStore::Filter{});
    CHECK(rows.size() == 2 && rows[0].date == d14 && rows[1].date == d13);
    CHECK(rows[0].Ap == 4 && rows[0].K[0] > 0.32 && rows[0].K[0] < 0.34 && rows[1].flux == 119 && rows[1].SSN == 118);
    CHECK(rows[1].xray == "B6.7" && rows[0].pf[0] == "0.2379/0.2741" && rows[0].fl[0].contains("01:41"));
    CHECK(rows[0].s[2] == bs[2]);   // whole, not cut to 25 characters
    CHECK(st.count() == 2);

    SolarStore::Filter f;
    f.apFrom = 5;
    CHECK(st.rows(f).isEmpty());
    f = {};
    f.useDates = true;
    f.from = d13;
    f.to = d13;
    f.apTo = 10;   // dates and Ap together (C# left out the AND: an SQL error)
    CHECK(st.rows(f).size() == 1);
    f = {};
    f.flareClass = "C";
    CHECK(st.rows(f).size() == 1);
    f.flareClass = "M";
    CHECK(st.rows(f).isEmpty());
    st.dropTestDatabase();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testGeo();
    testSgas();
    testLevels();
    testConditions();
    testGoes();
    testBursts();
    testStore();
    std::printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
