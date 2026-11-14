// WSPR Solar 2 window, offscreen: NOAA's files from tests/data (saved on 14 Nov 2026) instead of the
// internet, "now" 14 Nov 2026 20:40 UTC, and a SCRATCH database wspr_sol_uitest (never the real wspr_sol;
// skipped if there's no MySQL). Run with QT_QPA_PLATFORM=offscreen. SOLAR_SHOT_DIR=folder saves pictures.
#include "appfont.h"
#include "mainwindow.h"
#include "solarstore.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
                         else std::printf("ok   %s\n", #cond); } while (0)

template <typename T> static T *w(MainWindow &m, const char *name) { return m.findChild<T *>(name); }
static QString lab(MainWindow &m, const char *name) { return w<QLabel>(m, name)->text(); }
static QString cell(MainWindow &m, const char *grid, int r, int c)
{
    auto *v = w<QTableView>(m, grid);
    return v->model()->index(r, c).data().toString();
}
static int rows(MainWindow &m, const char *grid) { return w<QTableView>(m, grid)->model()->rowCount(); }

static bool waitUpdated(MainWindow &m)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&m, &MainWindow::updated, &loop, [&] { done = true; loop.quit(); });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    return done;
}

static void shot(MainWindow &m, const QString &name)
{
    const QString dir = qEnvironmentVariable("SOLAR_SHOT_DIR");
    if (!dir.isEmpty())
        m.grab().save(QDir(dir).filePath(name));
}

static QString copyData(const QTemporaryDir &dir, bool withAK)
{
    for (const QString &f : QDir(SOLAR_TEST_DATA).entryList(QDir::Files))
        if (withAK || f != "AK.txt")
            QFile::copy(QString(SOLAR_TEST_DATA) + "/" + f, dir.filePath(f));
    return dir.path();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("G4GCI-test");
    QApplication::setApplicationName("WSPR Solar 2 test");
    applyAppFont();

    SolarStore scratch(DbSettings{}, "wspr_sol_uitest");
    if (!scratch.createTestDatabase()) {
        std::printf("solarwindow_uitest: no scratch MySQL; skipped (%s)\n", qPrintable(scratch.lastError()));
        return 77;
    }
    qputenv("WSPRSOLAR_DBNAME", "wspr_sol_uitest");
    const QDateTime now(QDate(2026, 11, 14), QTime(20, 40), QTimeZone::utc());

    {
        QTemporaryDir d;
        MainWindow m;
        m.setNowForTest(now);
        m.setDataFolderForTest(copyData(d, true));
        m.resize(1222, 739);
        m.show();
        m.start();
        CHECK(waitUpdated(m));
        CHECK(m.windowTitle().contains("WSPR Solar 2") && m.windowTitle().contains("V.0.2.1"));

        // current data (C#: dataGridView1)
        CHECK(rows(m, "dataGridView1") == 2);
        CHECK(cell(m, "dataGridView1", 0, 0) == "4" && cell(m, "dataGridView1", 0, 1) == "Quiet");
        CHECK(cell(m, "dataGridView1", 0, 2) == "0.33" && cell(m, "dataGridView1", 0, 8) == "-1.00");
        CHECK(cell(m, "dataGridView1", 0, 10) == "119" && cell(m, "dataGridView1", 0, 11) == "118"
              && cell(m, "dataGridView1", 0, 12) == "B6.7");
        CHECK(cell(m, "dataGridView1", 1, 0) == "n/a" && cell(m, "dataGridView1", 1, 7) == "3"
              && cell(m, "dataGridView1", 1, 10) == "-");
        CHECK(lab(m, "label7") == "Boulder:");
        CHECK(lab(m, "datelabel") == "2026 Nov 14" && lab(m, "Internetlabel") == "Internet connection normal");
        CHECK(lab(m, "conditionlabel") == "SFI: 119 - Higher HF propagation: fair ...Sun: quiet");
        CHECK(lab(m, "stormconditionlabel") == "Likely normal - (no storm or blackout)");
        CHECK(lab(m, "SSNlabel") == "High");
        CHECK(lab(m, "GClabel") == "--" && lab(m, "SClabel") == "--" && lab(m, "RClabel") == "--");
        CHECK(lab(m, "Glabel") == "--" && lab(m, "Slabel") == "--" && lab(m, "Rlabel") == "--");
        CHECK(lab(m, "Burstwarninglabel") == "No significant radio bursts at present, but moderate bursts reported earlier");
        CHECK(lab(m, "currentBurstlabel") == "none or no significant bursts");
        CHECK(w<QPlainTextEdit>(m, "textBox1")->toPlainText().contains("Planetary(estimated Ap)"));
        CHECK(w<QPlainTextEdit>(m, "textBox2")->toPlainText().contains("10 cm 119"));
        shot(m, "solar_current.png");

        // radio burst summary (C#: Burstgridbutton)
        w<QPushButton>(m, "Burstgridbutton")->click();
        CHECK(w<QWidget>(m, "BurstgroupBox")->isVisible());
        CHECK(cell(m, "BurstdataGridView", 0, 2) == "15min Moderate\nWideband\nnone\nnone");
        CHECK(cell(m, "BurstdataGridView", 0, 3) == "insignificant\n\nnone\nnone");
        CHECK(cell(m, "BurstdataGridView", 0, 7).isEmpty());   // 21-00 hasn't started
        shot(m, "solar_bursts.png");
        w<QPushButton>(m, "Burstgridbutton")->click();
        CHECK(!w<QWidget>(m, "BurstgroupBox")->isVisible());

        // what was saved: yesterday's flux etc. and today's and yesterday's GOES values and bursts
        SolarStore::Filter all;
        const QList<SolarStore::Row> saved = m.store().rows(all);
        CHECK(saved.size() == 2 && saved[0].date == QDate(2026, 11, 14) && saved[1].date == QDate(2026, 11, 13));
        CHECK(saved[1].flux == 119 && saved[1].SSN == 118 && saved[1].xray == "B6.7" && saved[0].Ap == 4);
        CHECK(saved[0].pf[0] == "0.2379/0.2741" && saved[1].pf[0] == "0.2407/0.2514");
        CHECK(saved[0].fl[0] == "00:10/C1.4/0.0013\n01:41/C1.7/0.0026" && saved[0].fl[6] == "18:45/C2.2/0.0014");
        CHECK(saved[0].s[2] == "RSP/0804-0811/III/2\nRSP/0848-0852/III/2\nRSP/0848-0852/III/2");

        // the database view (C#: Switchbutton)
        w<QPushButton>(m, "Switchbutton")->click();
        CHECK(w<QWidget>(m, "groupBox2")->isVisible() && !w<QWidget>(m, "groupBox1")->isVisible());
        CHECK(rows(m, "dataGridView2") == 2);
        CHECK(cell(m, "dataGridView2", 0, 0) == "2026-11-14" && cell(m, "dataGridView2", 0, 2) == "0.33"
              && cell(m, "dataGridView2", 0, 4) == "1" && cell(m, "dataGridView2", 0, 10) == "-");
        CHECK(cell(m, "dataGridView2", 1, 10) == "119" && cell(m, "dataGridView2", 1, 12) == "B6.7");
        shot(m, "solar_database.png");

        w<QLineEdit>(m, "AptextBox1")->setText("5");   // Ap from 5: neither day
        w<QPushButton>(m, "filterbutton")->click();
        CHECK(rows(m, "dataGridView2") == 0 && w<QPushButton>(m, "filterbutton")->text() == "Clear");
        w<QPushButton>(m, "filterbutton")->click();
        CHECK(rows(m, "dataGridView2") == 2);
        w<QLineEdit>(m, "AptextBox1")->clear();

        // proton flux / flares / bursts (C#: changebutton)
        w<QPushButton>(m, "changebutton")->click();
        CHECK(w<QWidget>(m, "dataGridView3")->isVisible() && !w<QWidget>(m, "dataGridView2")->isVisible());
        CHECK(rows(m, "dataGridView3") == 6);   // two days: proton flux, flares, bursts
        CHECK(cell(m, "dataGridView3", 0, 0) == "2026-11-14" && cell(m, "dataGridView3", 0, 1) == "0.2379/0.2741");
        CHECK(cell(m, "dataGridView3", 1, 0) == "Solar flares:" && cell(m, "dataGridView3", 1, 7) == "18:45/C2.2/0.0014");
        CHECK(cell(m, "dataGridView3", 2, 0) == "Radio bursts:" && cell(m, "dataGridView3", 2, 3).startsWith("RSP/0804"));
        shot(m, "solar_extra.png");
        w<QPushButton>(m, "Burstbutton")->click();   // hide the burst rows
        CHECK(rows(m, "dataGridView3") == 4 && w<QPushButton>(m, "Burstbutton")->text() == "Show bursts");
        w<QPushButton>(m, "Burstbutton")->click();
        CHECK(rows(m, "dataGridView3") == 6);
        w<QComboBox>(m, "flarelistBox")->setCurrentText("M");   // flare class filter: no M flares saved
        CHECK(lab(m, "Clabel") == "M");
        w<QPushButton>(m, "filterbutton")->click();
        CHECK(rows(m, "dataGridView3") == 0);
        w<QComboBox>(m, "flarelistBox")->setCurrentText("C");
        w<QPushButton>(m, "filterbutton")->click();   // Clear
        w<QPushButton>(m, "filterbutton")->click();   // Apply with C
        CHECK(rows(m, "dataGridView3") == 4);   // both days had C flares; no burst rows when filtered
        w<QPushButton>(m, "filterbutton")->click();
        w<QPushButton>(m, "Eventsbutton")->click();
        CHECK(w<QWidget>(m, "EventsgroupBox")->isVisible()
              && w<QPlainTextEdit>(m, "textBox3")->toPlainText().contains("Edited Events for 2026 Nov 14"));
        w<QPushButton>(m, "Eventsbutton")->click();
        w<QPushButton>(m, "Switchbutton")->click();
        CHECK(w<QWidget>(m, "groupBox1")->isVisible());
    }

    {   // NOAA's AK.txt not to be had: the 30-day file, with Fredericksburg instead of Boulder
        QTemporaryDir d;
        MainWindow m;
        m.setNowForTest(now);
        m.setDataFolderForTest(copyData(d, false));
        m.show();
        m.start();
        CHECK(waitUpdated(m));
        CHECK(cell(m, "dataGridView1", 0, 0) == "4" && cell(m, "dataGridView1", 0, 2) == "0.33");
        CHECK(lab(m, "label7") == "Fredericksburg:" && cell(m, "dataGridView1", 1, 6) == "2");
    }

    {   // no internet at all: the database views still work, nothing is wiped
        MainWindow m;
        m.setNowForTest(now);
        m.setDataFolderForTest("/nonexistent-folder");
        m.show();
        m.start();
        QTimer::singleShot(500, &app, &QApplication::quit);
        app.exec();
        CHECK(lab(m, "Internetlabel").startsWith("Warning"));
        CHECK(m.store().rows(SolarStore::Filter{})[0].pf[0] == "0.2379/0.2741");
    }

    scratch.dropTestDatabase();
    std::printf(failures ? "solarwindow_uitest: %d FAILED\n" : "solarwindow_uitest: all passed\n", failures);
    return failures ? 1 : 0;
}
