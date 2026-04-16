#include "MainWindow.h"
#include "XdfLoader.h"
#include "LslReplayEngine.h"
#include "StreamChartView.h"
#include "TimelineWidget.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QApplication>
#include <QStackedWidget>
#include <QFont>
#include <QFileInfo>
#include <QElapsedTimer>

// --- XdfLoadWorker (background file loading) ---

void XdfLoadWorker::process()
{
    emit progress("Parsing XDF file...");
    auto *loader = new XdfLoader;
    if (!loader->load(m_filePath)) {
        emit error(loader->errorString());
        delete loader;
        return;
    }
    emit progress("Building stream views...");
    emit finished(loader);
}

// --- MainWindow ---

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("XDF LSL Replayer");
    resize(1200, 750);
    setAcceptDrops(true);

    // Central stacked layout: welcome page vs data view
    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Welcome / drop target label
    m_welcomeLabel = new QLabel;
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setText(
        "<div style='text-align:center;'>"
        "<p style='font-size:48px; color:#3a4a6a;'>\u2B07</p>"
        "<p style='font-size:18px; color:#8890a0; font-weight:600;'>Drop an XDF file here</p>"
        "<p style='font-size:13px; color:#5a5a70;'>or use File \u2192 Open</p>"
        "</div>"
    );
    m_welcomeLabel->setStyleSheet("background: #16161e; border: 2px dashed #2a2a3a; border-radius: 12px; margin: 40px;");

    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0); // indeterminate
    m_progressBar->setFixedHeight(22);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("Loading...");
    m_progressBar->hide();

    // Stream tabs
    m_streamTabs = new QTabWidget;
    m_streamTabs->hide();

    // Timeline
    m_timeline = new TimelineWidget;
    m_timeline->hide();

    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(m_welcomeLabel, 1);
    mainLayout->addWidget(m_streamTabs, 1);
    mainLayout->addWidget(m_timeline);

    // Footer
    auto *footer = new QLabel;
    footer->setText("Made with \u2764\uFE0F in Augmented Cognition Lab");
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet(
        "background: #12121a; color: #5a5a70; padding: 6px; font-size: 11px;"
    );
    footer->setFixedHeight(28);
    mainLayout->addWidget(footer);

    setCentralWidget(central);

    setupMenus();
    setupToolbar();
    setupStatusBar();

    connect(m_timeline, &TimelineWidget::loopRegionChanged,
            this, &MainWindow::onLoopRegionChanged);
}

MainWindow::~MainWindow()
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
    }
    if (m_loadThread) {
        m_loadThread->quit();
        m_loadThread->wait();
    }
}

void MainWindow::setupMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    auto *openAction = fileMenu->addAction(tr("&Open XDF..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    fileMenu->addSeparator();

    auto *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::setupToolbar()
{
    m_toolbar = addToolBar(tr("Playback"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(20, 20));

    m_playAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Play"));
    m_pauseAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPause), tr("Pause"));
    m_stopAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"));

    m_toolbar->addSeparator();

    m_loopAction = m_toolbar->addAction(tr("\u21BB Loop"));
    m_loopAction->setCheckable(true);
    m_loopAction->setChecked(false);

    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);
    connect(m_loopAction, &QAction::triggered, this, &MainWindow::onToggleLoop);

    m_playAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
    m_loopAction->setEnabled(false);
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready \u2014 drop an XDF file to begin"));
    m_timeLabel = new QLabel;
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_timeLabel);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const auto &url : event->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(".xdf", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const auto &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".xdf", Qt::CaseInsensitive)) {
            loadXdfFile(path);
            return;
        }
    }
}

void MainWindow::openFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open XDF File"), QString(),
        tr("XDF Files (*.xdf);;All Files (*)"));
    if (!path.isEmpty())
        loadXdfFile(path);
}

void MainWindow::setLoadingState(bool loading)
{
    m_progressBar->setVisible(loading);
    m_playAction->setEnabled(!loading && m_loader != nullptr);
    m_pauseAction->setEnabled(!loading && m_loader != nullptr);
    m_stopAction->setEnabled(!loading && m_loader != nullptr);
    m_loopAction->setEnabled(!loading && m_loader != nullptr);
}

void MainWindow::loadXdfFile(const QString &filePath)
{
    // Stop any existing replay
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
        m_replayEngine.reset();
    }

    // Cancel any running load
    if (m_loadThread) {
        m_loadThread->quit();
        m_loadThread->wait();
        delete m_loadThread;
        m_loadThread = nullptr;
    }

    clearViews();
    m_currentFilePath = filePath;

    QFileInfo fi(filePath);
    m_statusLabel->setText(QString("Loading %1 (%2 MB)...")
                               .arg(fi.fileName())
                               .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
    setLoadingState(true);
    m_progressBar->setFormat(QString("Loading %1...").arg(fi.fileName()));

    // Background loading
    m_loadThread = new QThread;
    auto *worker = new XdfLoadWorker(filePath);
    worker->moveToThread(m_loadThread);

    connect(m_loadThread, &QThread::started, worker, &XdfLoadWorker::process);
    connect(worker, &XdfLoadWorker::finished, this, &MainWindow::onFileLoaded);
    connect(worker, &XdfLoadWorker::error, this, &MainWindow::onFileLoadError);
    connect(worker, &XdfLoadWorker::progress, this, &MainWindow::onLoadProgress);
    connect(worker, &XdfLoadWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &XdfLoadWorker::error, worker, &QObject::deleteLater);
    connect(m_loadThread, &QThread::finished, m_loadThread, &QObject::deleteLater);

    m_loadThread->start();
}

void MainWindow::onLoadProgress(const QString &status)
{
    m_progressBar->setFormat(status);
    m_statusLabel->setText(status);
}

void MainWindow::onFileLoaded(XdfLoader *loader)
{
    m_loadThread = nullptr;
    m_loader.reset(loader);

    QFileInfo fi(m_currentFilePath);
    setWindowTitle(QString("XDF LSL Replayer \u2014 %1").arg(fi.fileName()));

    QString info = QString("%1 stream(s) \u00B7 %2s duration \u00B7 %3")
                       .arg(m_loader->streamCount())
                       .arg(m_loader->duration(), 0, 'f', 1)
                       .arg(fi.fileName());
    m_statusLabel->setText(info);

    // Timeline
    m_timeline->setDuration(m_loader->duration());
    m_timeline->show();

    // Replay engine
    m_replayEngine = std::make_unique<LslReplayEngine>();
    m_replayEngine->setStreams(&m_loader->streams(),
                                m_loader->globalMinTime(),
                                m_loader->globalMaxTime());

    connect(m_replayEngine.get(), &LslReplayEngine::playbackPositionChanged,
            this, &MainWindow::onPlaybackPositionChanged, Qt::QueuedConnection);
    connect(m_replayEngine.get(), &LslReplayEngine::playbackFinished,
            this, &MainWindow::onStop, Qt::QueuedConnection);

    // Build chart views
    m_progressBar->setFormat("Building charts...");
    buildStreamViews();

    m_welcomeLabel->hide();
    m_streamTabs->show();
    setLoadingState(false);
}

void MainWindow::onFileLoadError(const QString &msg)
{
    m_loadThread = nullptr;
    setLoadingState(false);
    m_statusLabel->setText("Load failed");
    QMessageBox::warning(this, tr("Error Loading XDF"), msg);
}

void MainWindow::buildStreamViews()
{
    if (!m_loader)
        return;

    for (int i = 0; i < m_loader->streamCount(); ++i) {
        const auto &stream = m_loader->stream(i);
        auto *chart = new StreamChartView(stream, m_loader->globalMinTime());
        QString tabLabel = QString::fromStdString(stream.name);
        if (stream.channelCount > 1)
            tabLabel += QString(" (%1ch)").arg(stream.channelCount);
        m_streamTabs->addTab(chart, tabLabel);
        m_chartViews.push_back(chart);
    }
}

void MainWindow::clearViews()
{
    m_chartViews.clear();
    m_streamTabs->clear();
    m_streamTabs->hide();
    m_timeline->hide();
    m_welcomeLabel->show();
}

void MainWindow::onPlay()
{
    if (m_replayEngine) {
        m_replayEngine->play();
        m_statusLabel->setText("Playing...");
    }
}

void MainWindow::onPause()
{
    if (m_replayEngine) {
        m_replayEngine->pause();
        m_statusLabel->setText("Paused");
    }
}

void MainWindow::onStop()
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
        m_statusLabel->setText("Stopped");
    }
    onPlaybackPositionChanged(0.0);
}

void MainWindow::onToggleLoop()
{
    if (m_replayEngine) {
        m_replayEngine->setLoopEnabled(m_loopAction->isChecked());
        m_statusLabel->setText(m_loopAction->isChecked() ? "Loop enabled" : "Loop disabled");
    }
}

void MainWindow::onPlaybackPositionChanged(double seconds)
{
    m_timeline->setPlaybackPosition(seconds);

    for (auto *chart : m_chartViews)
        chart->setPlaybackCursor(seconds);

    int mins = static_cast<int>(seconds) / 60;
    double secs = seconds - mins * 60;
    m_timeLabel->setText(QString("%1:%2")
                             .arg(mins)
                             .arg(secs, 5, 'f', 1, '0'));
}

void MainWindow::onLoopRegionChanged(double startSec, double endSec)
{
    if (m_replayEngine)
        m_replayEngine->setLoopRegion(startSec, endSec);
}
