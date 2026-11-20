// WSPR Solar 2 main window - see mainwindow.h. Handler names are the C# ones (Solar.cs).
#include "mainwindow.h"
#include "ftpget.h"
#include "ui_mainwindow.h"

#include "credentials.h"
#include "loaderror.h"
#include "msg.h"
#include "rtfhtml.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIntValidator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

#ifndef WSPRSOLAR_VERSION
#define WSPRSOLAR_VERSION "0"
#endif

namespace {

DbSettings loadDbSettings()
{
    DbSettings d;   // C#: 127.0.0.1, admin / wspr
    if (const auto pw = Security::readDbPassword())   // ~/DBcredential, written by WSPR Scheduler
        d.password = *pw;
    QSettings s;
    d.host = s.value("db/host", d.host).toString();
    d.port = s.value("db/port", d.port).toInt();
    d.user = s.value("db/user", d.user).toString();
    d.password = s.value("db/password", d.password).toString();
    return d;
}

QStandardItemModel *makeModel(QTableView *v, const QList<QPair<QString, int>> &cols, QObject *parent)
{
    auto *m = new QStandardItemModel(0, int(cols.size()), parent);
    for (int i = 0; i < cols.size(); ++i)
        m->setHeaderData(i, Qt::Horizontal, cols[i].first);
    v->setModel(m);
    for (int i = 0; i < cols.size(); ++i)
        v->setColumnWidth(i, cols[i].second);
    v->verticalHeader()->hide();
    v->horizontalHeader()->setHighlightSections(false);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    return m;
}

void addRow(QStandardItemModel *m, const QStringList &cells)
{
    QList<QStandardItem *> row;
    for (const QString &c : cells) {
        auto *it = new QStandardItem(c);
        it->setTextAlignment(Qt::AlignCenter);
        row << it;
    }
    m->appendRow(row);
}

QString stripped(QString s)   // " (G1)" -> "G1", "" -> "--" (C#: the "last 3h" labels)
{
    s = s.remove('(').remove(')').remove('-').trimmed();
    return s.isEmpty() ? QString("--") : s;
}

const QStringList kPeriods{"00-03", "03-06", "06-09", "09-12", "12-15", "15-18", "18-21", "21-00"};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
      m_store(loadDbSettings(), qEnvironmentVariable("WSPRSOLAR_DBNAME", "wspr_sol"))   // (tests: a scratch one)
{
    ui->setupUi(this);
    setWindowTitle("WSPR Solar 2                       V." WSPRSOLAR_VERSION "    GNU GPLv3 License");   // as C#
    m_net = new QNetworkAccessManager(this);

    // WinForms AutoSize = true: grow to fit the text (Qt fonts measure differently)
    for (QWidget *w : findChildren<QLabel *>())
        if (!w->property("fixedSize").toBool())
            static_cast<QLabel *>(w)->adjustSize();
    ui->datecheckBox->adjustSize();

    // hidden at start (C#: Visible = false in the designer)
    for (QWidget *w : {static_cast<QWidget *>(ui->groupBox2), static_cast<QWidget *>(ui->dataGridView3),
                       static_cast<QWidget *>(ui->groupBox4), static_cast<QWidget *>(ui->flarelabel),
                       static_cast<QWidget *>(ui->Burstbutton), static_cast<QWidget *>(ui->EventsgroupBox),
                       static_cast<QWidget *>(ui->Eventsbutton), static_cast<QWidget *>(ui->RBlabel),
                       static_cast<QWidget *>(ui->hamqslgroupBox), static_cast<QWidget *>(ui->BurstgroupBox)})
        w->hide();
    for (QPlainTextEdit *t : {ui->textBox1, ui->textBox2, ui->textBox3}) {
        t->setReadOnly(true);
        t->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    ui->AptextBox1->setValidator(new QIntValidator(0, 400, this));   // C#: KeyPress handlers, 0-400
    ui->AptextBox2->setValidator(new QIntValidator(0, 400, this));
    ui->SSNtextBox1->setValidator(new QIntValidator(0, 400, this));
    ui->SSNtextBox2->setValidator(new QIntValidator(0, 400, this));
    for (QDateEdit *d : {ui->dateTimePicker1, ui->dateTimePicker2}) {
        d->setDisplayFormat("d MMMM yyyy");
        d->setDate(QDate::currentDate());
    }
    setupGrids();

    // the hamqsl.com picture: a click opens their page (C#: a WebBrowser control)
    auto *hq = new QVBoxLayout(ui->hamqslgroupBox);
    hq->setContentsMargins(10, 20, 10, 10);
    auto *img = new QPushButton(ui->hamqslgroupBox);
    img->setObjectName("hamqslImage");
    img->setFlat(true);
    img->setCursor(Qt::PointingHandCursor);
    img->setToolTip("Open hamqsl.com in the browser");
    connect(img, &QPushButton::clicked, this, [this] { openLink(Solar::kHamqslPage); });
    hq->addWidget(img);
    m_hamqslImage = nullptr;

    m_loadError = new LoadErrorLabel("WSPR Solar 2", centralWidget());

    m_timer = new QTimer(this);   // C#: solartimer, 5 minutes
    m_timer->setInterval(300000);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::solartimer_Tick);

    auto *f1 = new QShortcut(QKeySequence(Qt::Key_F1), this);   // C#: HelpRequested
    connect(f1, &QShortcut::activated, this, &MainWindow::showHelp);

    connect(ui->getLatestbutton, &QPushButton::clicked, this, &MainWindow::getLatestbutton_Click);
    connect(ui->forceUpdatebutton, &QPushButton::clicked, this, &MainWindow::forceUpdatebutton_Click_1);
    connect(ui->Burstgridbutton, &QPushButton::clicked, this, &MainWindow::Burstgridbutton_Click);
    connect(ui->hamqslbutton, &QPushButton::clicked, this, &MainWindow::hamqslbutton_Click);
    connect(ui->Eventsbutton, &QPushButton::clicked, this, &MainWindow::Eventsbutton_Click);
    connect(ui->Burstbutton, &QPushButton::clicked, this, &MainWindow::Burstbutton_Click_1);
    connect(ui->flarelistBox, &QComboBox::currentIndexChanged, this, &MainWindow::flarelistBox_SelectedIndexChanged);
    connect(ui->changebutton, &QPushButton::clicked, this, &MainWindow::changebutton_Click);
    connect(ui->filterbutton, &QPushButton::clicked, this, &MainWindow::filterbutton_Click);
    connect(ui->Switchbutton, &QPushButton::clicked, this, &MainWindow::Switchbutton_Click);
    connect(ui->infobutton, &QPushButton::clicked, this, &MainWindow::infobutton_Click);
    connect(ui->linksbutton1, &QPushButton::clicked, this, &MainWindow::linksbutton1_Click);
    connect(ui->linksbutton2, &QPushButton::clicked, this, &MainWindow::linksbutton2_Click);
    connect(ui->AusSWSbutton, &QPushButton::clicked, this, &MainWindow::AusSWSbutton_Click);
    connect(ui->days30checkBox, &QCheckBox::toggled, this, &MainWindow::showGeoText);
}

MainWindow::~MainWindow()
{
    delete m_help;
    delete ui;
}

void MainWindow::setupGrids()
{
    m_grid1 = makeModel(ui->dataGridView1,
                        {{"Ap (est.)", 60}, {"Activity level", 120}, {"00-03", 65}, {"03-06", 65}, {"06-09", 65},
                         {"09-12", 65}, {"12-15", 65}, {"15-18", 65}, {"18-21", 65}, {"21-00", 65}, {"SFI", 52},
                         {"SSN", 53}, {"X-ray", 52}},
                        this);
    ui->dataGridView1->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // C#: ScrollBars.None
    ui->dataGridView1->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->dataGridView1->verticalHeader()->setDefaultSectionSize(23);

    QList<QPair<QString, int>> c2{{"Date", 90}, {"Ap", 70}};
    for (const QString &p : kPeriods)
        c2.append({p, 75});
    c2.append({{"SFI", 60}, {"SSN", 60}, {"X-ray", 60}});
    m_grid2 = makeModel(ui->dataGridView2, c2, this);
    ui->dataGridView2->verticalHeader()->setDefaultSectionSize(23);

    QList<QPair<QString, int>> c3{{"Date", 90}};
    for (const QString &p : kPeriods)
        c3.append({p, 126});
    m_grid3 = makeModel(ui->dataGridView3, c3, this);
    ui->dataGridView3->setWordWrap(true);   // C#: WrapMode True, AutoSizeRowsMode AllCells

    QList<QPair<QString, int>> cb;
    for (const QString &p : kPeriods)
        cb.append({p, 110});
    m_burstGrid = makeModel(ui->BurstdataGridView, cb, this);
    ui->BurstdataGridView->setWordWrap(true);
    ui->BurstdataGridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    for (QTableView *v : {ui->dataGridView1, ui->dataGridView2, ui->dataGridView3, ui->BurstdataGridView}) {
        QFont f = v->font();
        f.setPointSizeF(8);   // C#: DefaultCellStyle.Font = Segoe UI 8
        v->setFont(f);
    }
}

QDateTime MainWindow::nowUtc() const
{
    return m_testNow.isValid() ? m_testNow : QDateTime::currentDateTimeUtc();
}

QUrl MainWindow::urlFor(const QString &url) const
{
    if (m_testDir.isEmpty())
        return QUrl(url);
    return QUrl::fromLocalFile(QDir(m_testDir).filePath(QUrl(url).fileName()));
}

void MainWindow::fetch(const QString &url, std::function<void(bool, const QByteArray &)> done)
{
    QNetworkRequest req(urlFor(url));
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSPR-Solar-2/" WSPRSOLAR_VERSION);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [r, done] {
        r->deleteLater();
        const QByteArray data = r->readAll();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(r->error() == QNetworkReply::NoError && (status == 0 || status == 200) && !data.isEmpty(), data);
    });
}

void MainWindow::checkNOAA(std::function<void(bool)> done)
{
    auto finish = [this, done](bool ok) {
        showInternet(ok);
        if (done)
            done(ok);
    };
    if (!m_testDir.isEmpty()) {
        finish(QDir(m_testDir).exists());
        return;
    }
    QNetworkRequest req{QUrl(Solar::kNoaaRoot)};
    req.setTransferTimeout(15000);
    QNetworkReply *r = m_net->head(req);
    connect(r, &QNetworkReply::finished, this, [r, finish] {
        r->deleteLater();
        finish(r->error() == QNetworkReply::NoError);
    });
}

void MainWindow::start()
{
    m_dbOk = m_store.open();
    if (m_dbOk) {
        m_store.widenColumns();   // see solarstore.h (failing here only means bursts may not all be saved)
        m_store.saveDefault(nowUtc().date());   // C#: savedefaultdata - today's row
    } else {
        showDbError();
    }
    m_timer->start();
    checkNOAA([this](bool ok) {
        if (ok)
            fullUpdate();
        else
            Msg::timed(this, "Warning: unable to connect to NOAA", "Solar data", 1500);
    });
}

void MainWindow::showDbError()
{
    m_loadError->showError(m_store.lastError(), false);
}

void MainWindow::fetchGeo(std::function<void(bool, const QString &)> done)
{
    // AK.txt over FTP (as the C# version), then over https from the same server, else the 30-day file
    auto viaHttps = [this, done] {
        fetch(Solar::kGeoUrl, [this, done](bool ok, const QByteArray &d) {
            const QString t = QString::fromLatin1(d);
            if (ok && t.contains("Planetary(")) {
                done(true, t);
                return;
            }
            fetch(Solar::kGeoUrlAlt, [done](bool ok2, const QByteArray &d2) { done(ok2, QString::fromLatin1(d2)); });
        });
    };
    if (!m_testDir.isEmpty() && m_geoFtpForTest.isEmpty()) {   // tests: the files in m_testDir
        viaHttps();
        return;
    }
    auto *ftp = new FtpGet(this);
    connect(ftp, &FtpGet::finished, this, [ftp, done, viaHttps](bool ok, const QByteArray &d, const QString &) {
        ftp->deleteLater();
        const QString t = QString::fromLatin1(d);
        if (ok && t.contains("Planetary("))
            done(true, t);
        else
            viaHttps();
    });
    ftp->get(QUrl(m_geoFtpForTest.isEmpty() ? QString(Solar::kGeoFtpUrl) : m_geoFtpForTest), 20000);
}

void MainWindow::fullUpdate()
{
    if (m_busy)
        return;
    m_busy = true;
    for (QPushButton *b : {ui->getLatestbutton, ui->forceUpdatebutton}) {
        b->setEnabled(false);
        b->setText("Updating...");
    }
    auto f = std::make_shared<Files>();
    auto pending = std::make_shared<int>(6);
    auto oneDone = [this, f, pending] {
        if (--*pending > 0)
            return;
        // GOES: the secondary satellite if the primary's file is no good (C#: satErr)
        if (!f->protonsOk || Solar::protonFlux(f->protons, nowUtc().date(), 0).error) {
            fetch(Solar::protonUrl(false), [this, f](bool ok, const QByteArray &d) {
                if (ok)
                    f->protons2 = d;
                process(*f);
            });
            return;
        }
        process(*f);
    };
    fetchGeo([f, oneDone](bool ok, const QString &t) { f->geoOk = ok; f->geo = t; oneDone(); });
    fetch(Solar::kGeoUrlAlt, [f, oneDone](bool ok, const QByteArray &d) {   // for the "30 day" box
        if (ok)
            f->daily = QString::fromLatin1(d);
        oneDone();
    });
    fetch(Solar::kSgasUrl, [f, oneDone](bool ok, const QByteArray &d) {
        f->sgasOk = ok;
        f->sgas = QString::fromLatin1(d);
        oneDone();
    });
    fetch(Solar::kEventsUrl, [f, oneDone](bool ok, const QByteArray &d) {
        f->eventsOk = ok;
        f->events = QString::fromLatin1(d);
        oneDone();
    });
    fetch(Solar::protonUrl(true), [f, oneDone](bool ok, const QByteArray &d) {
        f->protonsOk = ok;
        f->protons = d;
        oneDone();
    });
    fetch(Solar::kFlaresUrl, [f, oneDone](bool ok, const QByteArray &d) {
        f->flaresOk = ok;
        f->flares = d;
        oneDone();
    });
}

void MainWindow::process(const Files &f)
{
    const QDateTime now = nowUtc();
    const QDate today = now.date(), yesterday = today.addDays(-1);
    if (m_dbOk || m_store.open()) {
        if (!m_dbOk) {   // the database came back
            m_dbOk = true;
            m_loadError->hide();
            m_store.widenColumns();
        }
        m_store.saveDefault(today);
    }
    const bool anyOk = f.geoOk || f.sgasOk || f.eventsOk || f.protonsOk || f.flaresOk;
    if (anyOk)
        m_lastUpdate = nowUtc();
    showInternet(anyOk);
    setLabel(ui->datelabel, Solar::geoDateKey(today));

    // A and K indices (C#: getLatestSolar, updateGeo)
    if (f.geoOk) {
        m_akText = f.geo.contains("Planetary(") ? f.geo : QString();
        if (!f.daily.isEmpty())
            m_dailyText = f.daily;
        else if (m_akText.isEmpty())
            m_dailyText = f.geo;
        showGeoText();
        m_planetary = Solar::findGeo(f.geo, today, "Planetary");
        m_station = Solar::findGeo(f.geo, today, "Boulder");
        const Solar::Geo y = Solar::findGeo(f.geo, yesterday, "Planetary");
        m_prevG = y.found ? Solar::kpLevel(y.K[7]) : QString();
        if (m_dbOk) {
            if (y.found)
                m_store.saveGeo(yesterday, y);
            if (m_planetary.found)
                m_store.saveGeo(today, m_planetary);
        }
    }

    // yesterday's solar flux, sunspot number and X-ray background (C#: updateSolar)
    if (f.sgasOk) {
        ui->textBox2->setPlainText(f.sgas);
        m_daily = Solar::findSolar(f.sgas, today);
        if (m_daily.found && m_dbOk)
            m_store.saveSolar(yesterday, m_daily);   // NOAA's figures are yesterday's
    }
    if (!m_daily.found && m_dbOk) {
        // before NOAA's report of the day (about 02:45 UTC) C# showed "unknown": use the last ones saved
        for (const SolarStore::Row &r : m_store.rows(SolarStore::Filter{}, 5))
            if (r.flux > 0) {
                m_daily.flux = QString::number(r.flux, 'f', 0);
                m_daily.ssn = QString::number(r.SSN);
                m_daily.xray = r.xray;
                break;
            }
    }

    // GOES proton flux and flares by 3-hour period, yesterday and today (C#: updateAllProtonandFlare)
    if (m_dbOk) {
        for (const QDate &day : {yesterday, today}) {
            std::array<QString, 8> pf, fl;
            bool pfOk = f.protonsOk || !f.protons2.isEmpty();
            for (int i = 0; i < 8; ++i) {
                Solar::PeriodResult p = Solar::protonFlux(f.protons, day, i * 3);
                if (p.error && !f.protons2.isEmpty())
                    p = Solar::protonFlux(f.protons2, day, i * 3);
                if (p.error)
                    pfOk = false;
                pf[i] = p.text;
                const Solar::PeriodResult fr = Solar::flares(f.flares, day, i * 3);
                fl[i] = fr.text;
            }
            if (pfOk)   // C# saved empty values when the file couldn't be read, wiping what was there
                m_store.saveSlots(day, SolarStore::Column::ProtonFlux, pf);
            if (f.flaresOk && !Solar::flares(f.flares, day, 0).error)
                m_store.saveSlots(day, SolarStore::Column::Flares, fl);
        }
    }

    // radio bursts so far today (C#: updateBursts)
    if (f.eventsOk) {
        m_eventsText = f.events;
        ui->textBox3->setPlainText(f.events);
        const Solar::BurstSlots b = Solar::findBursts(f.events, today);
        if (b.found && m_dbOk)
            m_store.saveSlots(today, SolarStore::Column::Bursts, b.slot);
    }

    showCurrent();
    showDatabase();
    showExtra();
    showStorms();
    showBursts();
    m_busy = false;
    ui->getLatestbutton->setText("Get Update");
    ui->forceUpdatebutton->setText("Force update");
    for (QPushButton *b : {ui->getLatestbutton, ui->forceUpdatebutton})
        b->setEnabled(true);
    emit updated();
}

// ---- grids and labels ---------------------------------------------------------------------------

void MainWindow::showInternet(bool ok)
{
    QString t = ok ? "Internet connection normal" : "Warning: Internet connection may be down";
    if (m_lastUpdate.isValid())
        t += m_lastUpdate.toString("  -  'updated' HH:mm 'UTC'");
    setLabel(ui->Internetlabel, t);
}

void MainWindow::showGeoText()
{
    // unticked: yesterday and today - AK.txt, or those two days of the 30-day file; ticked: 30 days,
    // newest at the bottom, shown from there
    const bool days30 = ui->days30checkBox->isChecked() && !m_dailyText.isEmpty();
    if (days30)
        ui->textBox1->setPlainText(m_dailyText);
    else if (!m_akText.isEmpty())
        ui->textBox1->setPlainText(m_akText);
    else
        ui->textBox1->setPlainText(Solar::lastDays(m_dailyText, 2));
    setLabel(ui->label2, days30 ? "Geomagnetic data (last 30 days)" : "Geomagnetic data (yesterday + today)");
    if (days30)
        ui->textBox1->moveCursor(QTextCursor::End);
}

void MainWindow::setLabel(QLabel *l, const QString &text)
{
    l->setText(text);
    l->adjustSize();
}

void MainWindow::showCurrent()
{
    const int hour = nowUtc().time().hour();
    m_grid1->removeRows(0, m_grid1->rowCount());
    auto kCells = [](const Solar::Geo &g) {
        QStringList c;
        for (const QString &k : g.K)
            c << k + Solar::kpLevel(k);
        return c;
    };
    // planetary (C#: populateGrid(0))
    if (m_planetary.found) {
        const QString act = Solar::activityLevel(m_planetary.Ap, hour);
        m_activity = act.toLower();
        addRow(m_grid1, QStringList{m_planetary.Ap, act} + kCells(m_planetary)
                            + QStringList{m_daily.flux, m_daily.ssn, m_daily.xray});
    } else {
        m_activity = "unknown";
        QStringList blank(10, "--");
        addRow(m_grid1, blank + QStringList{m_daily.flux, m_daily.ssn, m_daily.xray});
    }
    // Boulder, or Fredericksburg when NOAA's AK.txt isn't to be had (C#: populateGrid(1))
    if (m_station.found) {
        const bool a = m_station.Ap != "-1";
        addRow(m_grid1, QStringList{a ? m_station.Ap : "n/a", a ? Solar::activityLevel(m_station.Ap, hour) : "n/a"}
                            + kCells(m_station) + QStringList{"-", "-", "-"});
        setLabel(ui->label7, m_station.station + ":");
    } else {
        addRow(m_grid1, QStringList(13, "-"));
    }
    ui->label7->move(ui->classlabel->geometry().right() - ui->label7->width(), ui->label7->y());   // right-aligned

    // "Last 3h" G (C#: find_current_G; before 03 UTC yesterday's last period)
    QString g;
    if (hour < 3)
        g = m_prevG;
    else if (m_planetary.found)
        g = Solar::kpLevel(m_planetary.K[Solar::lastPeriod(hour, true)]);
    setLabel(ui->GClabel, stripped(g));
    setLabel(ui->SSNlabel, Solar::ssnLevel(m_daily.ssn));
}

void MainWindow::showDatabase()
{
    m_grid2->removeRows(0, m_grid2->rowCount());
    if (!m_dbOk)
        return;
    const QDate today = nowUtc().date();
    const SolarStore::Filter f = m_filtered && !ui->dataGridView3->isVisible() ? filterFromUi(false) : SolarStore::Filter{};
    for (const SolarStore::Row &r : m_store.rows(f)) {
        QStringList c{r.date.toString("yyyy-MM-dd"), QString::number(r.Ap)};
        for (double k : r.K) {
            const QString t = Solar::kpText(k);
            c << t + Solar::kpLevel(t);
        }
        if (r.date == today)   // today's are known tomorrow
            c << "-" << "-" << "-";
        else
            c << QString::number(r.flux, 'f', 0) << QString::number(r.SSN) << r.xray;
        addRow(m_grid2, c);
    }
}

void MainWindow::showExtra()
{
    m_grid3->removeRows(0, m_grid3->rowCount());
    if (!m_dbOk)
        return;
    const SolarStore::Filter f = m_filtered && ui->dataGridView3->isVisible() ? filterFromUi(true) : SolarStore::Filter{};
    for (const SolarStore::Row &r : m_store.rows(f)) {
        QStringList pf{r.date.toString("yyyy-MM-dd")}, fl{"Solar flares:"}, bs{"Radio bursts:"};
        for (int i = 0; i < 8; ++i) {
            pf << r.pf[i] + Solar::protonS(r.pf[i]);
            fl << r.fl[i] + Solar::flareR(r.fl[i]);
            bs << r.s[i];
        }
        addRow(m_grid3, pf);
        addRow(m_grid3, fl);
        if (m_showBurstRows && !m_filtered)   // C#: no burst rows with a filter applied
            addRow(m_grid3, bs);
    }
    for (int row = 0; row < m_grid3->rowCount(); ++row) {
        m_grid3->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        for (int c = 1; c < m_grid3->columnCount(); ++c)
            m_grid3->item(row, c)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    ui->dataGridView3->resizeRowsToContents();
}

void MainWindow::showStorms()
{
    const QDateTime now = nowUtc();
    const int hour = now.time().hour();
    std::array<QString, 8> pf, fl;
    if (m_dbOk) {
        SolarStore::Filter t;
        t.useDates = true;
        t.from = t.to = now.date();
        const QList<SolarStore::Row> rows = m_store.rows(t, 1);
        if (!rows.isEmpty()) {
            pf = rows[0].pf;
            fl = rows[0].fl;
        }
    }
    // the last 3 hours (C#: find_current_S / find_current_R) and anything today (C#: findstormlevels)
    const int last = Solar::lastPeriod(hour, false);
    setLabel(ui->SClabel, stripped(Solar::protonS(pf[last])));
    setLabel(ui->RClabel, stripped(Solar::flareR(fl[last])));
    int g = 0, s = 0, r = 0;
    for (int i = 0; i < 8; ++i) {
        s = qMax(s, Solar::levelDigit(Solar::protonS(pf[i]), 'S'));
        r = qMax(r, Solar::levelDigit(Solar::flareR(fl[i]), 'R'));
    }
    for (int row = 0; row < m_grid1->rowCount(); ++row)
        for (int c = 2; c < 10; ++c)
            g = qMax(g, Solar::levelDigit(m_grid1->item(row, c)->text(), 'G'));
    setLabel(ui->Glabel, g > 0 ? QString("G%1").arg(g) : "--");
    setLabel(ui->Slabel, s > 0 ? QString("S%1").arg(s) : "--");
    setLabel(ui->Rlabel, r > 0 ? QString("R%1").arg(r) : "--");
    setLabel(ui->stormlabel, m_activity.contains("storm") ? m_activity : "--");

    const Solar::Conditions c = Solar::conditions(m_daily.flux, m_activity, ui->GClabel->text(), ui->RClabel->text(),
                                                  ui->SClabel->text(), ui->Glabel->text(), ui->Rlabel->text(),
                                                  ui->Slabel->text());
    setLabel(ui->conditionlabel, c.condition);
    setLabel(ui->stormconditionlabel, c.storm);
    setLabel(ui->SSNlabel, Solar::ssnLevel(m_daily.ssn));
}

void MainWindow::showBursts()
{
    const QDateTime now = nowUtc();
    std::array<QString, 8> burstSlots;
    if (m_dbOk) {
        SolarStore::Filter t;
        t.useDates = true;
        t.from = t.to = now.date();
        const QList<SolarStore::Row> rows = m_store.rows(t, 1);
        if (!rows.isEmpty())
            burstSlots = rows[0].s;
    } else {
        burstSlots = Solar::findBursts(m_eventsText, now.date()).slot;
    }
    const int p = Solar::periodOfHour(now.time().hour());
    m_burstGrid->removeRows(0, m_burstGrid->rowCount());
    QStringList cells;
    for (int i = 0; i < 8; ++i) {
        m_bd[i] = i <= p ? Solar::parseBurstPeriod(burstSlots[i]) : Solar::BurstPeriod{};   // later periods: blank
        cells << (i <= p ? Solar::burstCellText(m_bd[i]) : QString());
    }
    addRow(m_burstGrid, cells);
    ui->BurstdataGridView->resizeRowsToContents();
    alignBurstHeadings();
    const Solar::BurstNow n = Solar::currentBurst(m_bd, p);
    setLabel(ui->Burstwarninglabel, n.warning);
    setLabel(ui->currentBurstlabel, n.current);
}

void MainWindow::alignBurstHeadings()
{
    // C# placed these at fixed positions to match its grid; Qt's header height and line spacing differ
    // by platform and font, so they're put level with the grid's four lines of text as drawn
    QTableView *v = ui->BurstdataGridView;
    if (m_burstGrid->rowCount() == 0)
        return;
    const QFontMetrics fm(v->font());
    const int ls = fm.lineSpacing();
    const QRect cell = v->visualRect(m_burstGrid->index(0, 0));   // in the grid's viewport
    const int top = v->y() + v->viewport()->y() + cell.top() + (cell.height() - 4 * ls) / 2;
    const QList<QLabel *> heads{ui->label27, ui->label29, ui->label31, ui->label32};
    for (int i = 0; i < heads.size(); ++i) {
        QLabel *l = heads[i];
        l->setFont(v->font());
        l->adjustSize();
        l->move(v->x() - l->width() - 4, top + i * ls + (ls - l->height()) / 2);
    }
}

SolarStore::Filter MainWindow::filterFromUi(bool extra) const
{
    SolarStore::Filter f;
    if (ui->datecheckBox->isChecked()) {
        f.useDates = true;
        f.from = ui->dateTimePicker1->date();
        f.to = ui->dateTimePicker2->date();
    }
    if (extra) {
        f.flareClass = ui->flarelistBox->currentText();
        return f;
    }
    auto num = [](const QLineEdit *e) { return e->text().isEmpty() ? -1 : e->text().toInt(); };
    f.apFrom = num(ui->AptextBox1);
    f.apTo = num(ui->AptextBox2);
    f.ssnFrom = num(ui->SSNtextBox1);
    f.ssnTo = num(ui->SSNtextBox2);
    return f;
}

void MainWindow::openLink(const QString &url)
{
    if (!QDesktopServices::openUrl(QUrl(url)))
        Msg::timed(this, "Couldn't open the browser", "Info", 2000);
}

// ---- buttons ------------------------------------------------------------------------------------

void MainWindow::getLatestbutton_Click()
{
    checkNOAA([this](bool ok) {
        if (ok)
            fullUpdate();
        else
            Msg::timed(this, "Unable to connect to NOAA url", "Solar data", 1000);
    });
}

void MainWindow::forceUpdatebutton_Click_1()
{
    getLatestbutton_Click();
}

void MainWindow::Switchbutton_Click()
{
    const bool toDb = ui->Switchbutton->text() == "Database";
    ui->Switchbutton->setText(toDb ? "Current data" : "Database");
    ui->groupBox1->setVisible(!toDb);
    ui->groupBox2->setVisible(toDb);
    if (toDb) {
        showDatabase();
        showExtra();
    }
}

void MainWindow::changebutton_Click()   // C#: between the Ap/Kp/SSN and the PF/flare/burst grids
{
    const bool toExtra = ui->changebutton->text().startsWith("PF");
    ui->changebutton->setText(toExtra ? "Ap/Kp/SSN indices" : "PF/flare/burst data");
    ui->groupBox3->setVisible(!toExtra);
    ui->groupBox4->setVisible(toExtra);
    ui->dataGridView2->setVisible(!toExtra);
    ui->dataGridView3->setVisible(toExtra);
    setLabel(ui->toplabel, toExtra ? QString::fromUtf8("Proton flux (protons/cm²·s·sr)>=10MeV (avg/max):") : "Kp indices:");
    setLabel(ui->flarelabel, QString::fromUtf8("Flares: max time/ max class/int xrlong (J/m²)"));
    ui->flarelabel->setVisible(toExtra);
    ui->Eventsbutton->setVisible(toExtra);
    ui->Burstbutton->setVisible(toExtra && !m_filtered);
    ui->RBlabel->setVisible(toExtra && m_showBurstRows);
    if (m_filtered)
        toExtra ? showExtra() : showDatabase();
}

void MainWindow::filterbutton_Click()
{
    m_filtered = ui->filterbutton->text() == "Apply";
    ui->filterbutton->setText(m_filtered ? "Clear" : "Apply");
    ui->Burstbutton->setVisible(!m_filtered && ui->dataGridView3->isVisible());
    if (ui->dataGridView3->isVisible())
        showExtra();
    else
        showDatabase();
}

void MainWindow::Burstbutton_Click_1()   // C#: showBursts - the radio-burst rows in the PF/flare grid
{
    m_showBurstRows = !m_showBurstRows;
    ui->Burstbutton->setText(m_showBurstRows ? "Hide bursts" : "Show bursts");
    ui->RBlabel->setVisible(m_showBurstRows);
    showExtra();
}

void MainWindow::Eventsbutton_Click()
{
    if (ui->Eventsbutton->text() == "Current Summary") {
        ui->EventsgroupBox->show();
        ui->EventsgroupBox->raise();
        ui->Eventsbutton->setText("Hide");
        fetch(Solar::kEventsUrl, [this](bool ok, const QByteArray &d) {
            if (ok) {
                m_eventsText = QString::fromLatin1(d);
                ui->textBox3->setPlainText(m_eventsText);
            }
        });
    } else {
        ui->Eventsbutton->setText("Current Summary");
        ui->EventsgroupBox->hide();
    }
}

void MainWindow::Burstgridbutton_Click()
{
    if (ui->Burstgridbutton->text() == "Radio bursts") {
        showBursts();
        ui->BurstgroupBox->show();
        ui->BurstgroupBox->raise();
        alignBurstHeadings();
        ui->Burstgridbutton->setText("Hide bursts");
    } else {
        ui->BurstgroupBox->hide();
        ui->Burstgridbutton->setText("Radio bursts");
    }
}

void MainWindow::hamqslbutton_Click()
{
    if (ui->hamqslbutton->text() != "Forecast") {
        ui->hamqslbutton->setText("Forecast");
        ui->hamqslgroupBox->hide();
        return;
    }
    ui->hamqslbutton->setText("Hide");
    ui->hamqslgroupBox->setTitle("Solar-Terrestrial Data - courtesy of N0NBH at hamqsl.com");
    ui->hamqslgroupBox->show();
    ui->hamqslgroupBox->raise();
    auto *img = ui->hamqslgroupBox->findChild<QPushButton *>("hamqslImage");
    img->setText("Loading...");
    fetch(Solar::kHamqslImage, [this, img](bool ok, const QByteArray &d) {
        QPixmap pm;
        if (ok && pm.loadFromData(d)) {
            img->setText({});
            img->setIcon(QIcon(pm));
            img->setIconSize(pm.size());
        } else {
            img->setText("Unable to reach hamqsl.com - click to open their page");
        }
    });
}

void MainWindow::flarelistBox_SelectedIndexChanged(int index)
{
    setLabel(ui->Clabel, index > 0 ? ui->flarelistBox->currentText() : "--");
}

void MainWindow::infobutton_Click() { openLink("https://www.swpc.noaa.gov/noaa-scales-explanation"); }
void MainWindow::linksbutton1_Click()
{
    openLink("https://www.qsl.net/4/4x4xm//Books-and-Articles/Understanding-Solar-Indices-Ian-Poole-G3YWX-QST-September-2002.pdf");
}
void MainWindow::linksbutton2_Click() { openLink("https://rsgb.org/main/technical/propagation/"); }
void MainWindow::AusSWSbutton_Click() { openLink("https://www.sws.bom.gov.au/"); }

void MainWindow::solartimer_Tick()
{
    ++m_timerCount;
    checkNOAA([this](bool ok) {
        if (!ok)
            return;
        if (m_timerCount == 2 && ui->conditionlabel->text().trimmed().isEmpty())   // nothing at start-up
            fullUpdate();
        if (m_timerCount >= 6) {   // every 30 minutes
            m_timerCount = 0;
            fullUpdate();
        }
    });
}

// ---- help (F1) ----------------------------------------------------------------------------------

QString MainWindow::helpFile(const QStringList &dirs)
{
    static const QRegularExpression re("^About_WS_Solar(?:_v(\\d+))?\\.rtf$", QRegularExpression::CaseInsensitiveOption);
    QString best;
    int bestVersion = -1;
    QDateTime bestTime;
    for (const QString &d : dirs)
        for (const QString &name : QDir(d).entryList({"About_WS_Solar*.rtf"}, QDir::Files)) {
            const auto m = re.match(name);
            if (!m.hasMatch())
                continue;
            const int v = m.captured(1).toInt();   // no "_vN": 0
            const QString path = QDir(d).filePath(name);
            const QDateTime t = QFileInfo(path).lastModified();
            // the highest version; of the same version, the one saved last (an edited copy next to the
            // program beats an older one in C:\WSPR_Sked)
            if (v > bestVersion || (v == bestVersion && t > bestTime)) {
                bestVersion = v;
                bestTime = t;
                best = path;
            }
        }
    return best;
}

void MainWindow::showHelp()
{
    // The newest About_WS_Solar_vN.rtf in C:\WSPR_Sked (~/WSPR_Sked), next to the program, or where
    // "cmake --install" puts it - so a newer one next to the program wins over the C# version's v3
    // (C#: C:\WSPR_Sked\About_WS_Solar_v3.rtf, else next to the program)
#ifdef Q_OS_WIN
    const QString sked = "C:/WSPR_Sked";
#else
    const QString sked = QDir::home().filePath("WSPR_Sked");
#endif
    const QString file = helpFile(
        {sked, QCoreApplication::applicationDirPath(), QDir(QCoreApplication::applicationDirPath()).filePath("../share/wspr-solar-2")});
    QFile f(file);
    if (file.isEmpty() || !f.open(QIODevice::ReadOnly)) {
        Msg::timed(this, "About_WS_Solar_v5.rtf not found in " + QDir::toNativeSeparators(sked)
                             + " or next to WSPR Solar 2", "Help file", 3000);
        return;
    }
    const Rtf::Html h = Rtf::toHtml(f.readAll());
    if (!m_help) {
        m_help = new QWidget(nullptr, Qt::Window);
        m_help->setWindowTitle("WSPR Solar 2 - Help");   // (+ the file, below)
        m_help->setWindowIcon(windowIcon());
        auto *lay = new QVBoxLayout(m_help);
        lay->setContentsMargins(0, 0, 0, 0);
        m_helpText = new QTextBrowser(m_help);
        m_helpText->setOpenExternalLinks(true);
        lay->addWidget(m_helpText);
        m_help->resize(760, 640);
    }
    m_help->setWindowTitle("WSPR Solar 2 - Help - " + QDir::toNativeSeparators(file));   // which file it is
    QTextDocument *doc = m_helpText->document();
    doc->clear();
    for (const Rtf::Image &img : h.images)   // shrunk smoothly (see rtfhtml.h)
        doc->addResource(QTextDocument::ImageResource, QUrl(img.name), img.forDisplay(m_help->devicePixelRatioF()));
    m_helpText->setHtml(h.ok ? h.html : QString::fromUtf8(f.readAll()));
    m_helpText->moveCursor(QTextCursor::Start);
    m_help->show();
    m_help->raise();
    m_help->activateWindow();
}
