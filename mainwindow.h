#pragma once
// WSPR Solar 2 main window - Qt 6 port of the C# Solar form (Solar.cs). Control and handler names are
// kept from C#; the work is in solarcore (NOAA files, levels) and solarstore (the wspr_sol database).
//
// How it runs (C#: setConfig at start-up, then solartimer every 5 minutes):
//  - at start: check NOAA can be reached, add today's row to wspr_sol, then a full update;
//  - a full update fetches NOAA's five files at once (C# fetched each several times, one after another,
//    on the window's thread), saves yesterday's and today's values, and fills every grid and label;
//  - every 5 minutes: check NOAA can still be reached; every 30 minutes: a full update (C# spread the
//    parts over 25-45 minutes); 10 minutes after start, another if the first found nothing.
#include "solarcore.h"
#include "solarstore.h"

#include <QMainWindow>
#include <QPointer>
#include <array>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
class LoadErrorLabel;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QStandardItemModel;
class QTableView;
class QTextBrowser;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void start();   // C#: Solar_Load / setConfig - after show()

    // tests: the time "now" (UTC), and where NOAA's files are read from (a folder: file names as NOAA's)
    void setNowForTest(const QDateTime &utc) { m_testNow = utc; }
    void setDataFolderForTest(const QString &dir) { m_testDir = dir; }
    bool busy() const { return m_busy; }
    SolarStore &store() { return m_store; }

signals:
    void updated();   // a full update has finished (tests)

private slots:
    void getLatestbutton_Click();
    void forceUpdatebutton_Click_1();
    void Burstgridbutton_Click();
    void hamqslbutton_Click();
    void Eventsbutton_Click();
    void Burstbutton_Click_1();
    void flarelistBox_SelectedIndexChanged(int index);
    void changebutton_Click();
    void filterbutton_Click();
    void Switchbutton_Click();
    void infobutton_Click();
    void linksbutton1_Click();
    void linksbutton2_Click();
    void AusSWSbutton_Click();
    void solartimer_Tick();
    void showHelp();   // C#: Solar_HelpRequested (F1)

public:
    // the newest About_WS_Solar_vN.rtf in these folders (tests)
    static QString helpFile(const QStringList &dirs);

private slots:

private:
    struct Files
    {
        QString geo, daily, sgas, events;   // geo: AK.txt, or the 30-day file if AK.txt failed
        QByteArray protons, protons2, flares;
        bool geoOk = false, sgasOk = false, eventsOk = false, protonsOk = false, flaresOk = false;
    };

    QDateTime nowUtc() const;
    QUrl urlFor(const QString &url) const;
    void fetch(const QString &url, std::function<void(bool ok, const QByteArray &data)> done);
    void checkNOAA(std::function<void(bool)> done = {});
    void fullUpdate();                       // C#: getLatestSolar + updateGeo + updateSolar + updateAllProtonandFlare
    void process(const Files &f);
    void fetchGeo(std::function<void(bool, const QString &)> done);
    void showGeoText();                      // textBox1: 2 days, or 30 with the "30 day" box ticked

    // grids and labels
    void setupGrids();
    void showCurrent();                      // dataGridView1 (C#: populateGrid)
    void showDatabase();                     // dataGridView2 (C#: find_data)
    void showExtra();                        // dataGridView3 (C#: find_extra_data + find_burst_data)
    void showStorms();                       // G/S/R labels and the conditions (C#: findstormlevels, stormlabels)
    void showBursts();                       // BurstdataGridView and the burst labels (C#: findBurstStorms)
    void alignBurstHeadings();               // the four row headings beside the burst grid, line by line
    void setLabel(QLabel *l, const QString &text);
    void showInternet(bool ok);              // Internetlabel, with the time of the last update
    void openLink(const QString &url);
    SolarStore::Filter filterFromUi(bool extra) const;
    void showDbError();

    Ui::MainWindow *ui;
    SolarStore m_store;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_timer = nullptr;
    int m_timerCount = 0;
    bool m_busy = false;
    bool m_dbOk = false;
    bool m_filtered = false;
    bool m_showBurstRows = true;   // C#: "Hide bursts" after start-up
    LoadErrorLabel *m_loadError = nullptr;

    QStandardItemModel *m_grid1 = nullptr, *m_grid2 = nullptr, *m_grid3 = nullptr, *m_burstGrid = nullptr;

    // the latest values (C#: solar, SFI, SSN, activity_level, bd)
    Solar::Geo m_planetary, m_station;
    Solar::Daily m_daily;
    QString m_activity;
    QString m_prevG;
    std::array<Solar::BurstPeriod, 8> m_bd;
    QString m_eventsText;
    QString m_akText, m_dailyText;           // NOAA's AK.txt (when it can be had) and 30-day file

    QDateTime m_lastUpdate;                  // the last full update (UTC)
    QDateTime m_testNow;
    QString m_testDir;
    QPointer<QWidget> m_help;
    QTextBrowser *m_helpText = nullptr;
    QLabel *m_hamqslImage = nullptr;
};
