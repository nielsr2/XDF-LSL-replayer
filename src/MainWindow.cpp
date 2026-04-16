#include "MainWindow.h"
#include "XdfLoader.h"
#include "LslReplayEngine.h"
#include "StreamChartView.h"
#include "TimelineWidget.h"
#include "StreamSidebar.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStyle>
#include <QApplication>
#include <QFont>
#include <QFileInfo>

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
    emit progress(QString("Loaded %1 stream(s)").arg(loader->streamCount()));
    emit finished(loader);
}

// --- MainWindow ---

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("XDF LSL Replayer");
    resize(1280, 780);
    setAcceptDrops(true);

    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0);
    m_progressBar->setFixedHeight(22);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("Loading...");
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

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
    m_welcomeLabel->setStyleSheet(
        "background: #16161e; border: 2px dashed #2a2a3a; border-radius: 12px; margin: 40px;");

    // Sidebar
    m_sidebar = new StreamSidebar;
    m_sidebar->hide();

    // Stream tabs (lazy chart building)
    m_streamTabs = new QTabWidget;
    m_streamTabs->hide();
    connect(m_streamTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // Splitter: sidebar | charts
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_streamTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 1000});
    splitter->setStyleSheet(
        "QSplitter::handle { background: #2a2a3a; width: 2px; }");
    splitter->hide();
    m_sidebar->show(); // shown inside splitter

    mainLayout->addWidget(m_welcomeLabel, 1);
    mainLayout->addWidget(splitter, 1);

    // Timeline
    m_timeline = new TimelineWidget;
    m_timeline->hide();
    mainLayout->addWidget(m_timeline);

    // Footer
    auto *footer = new QLabel;
    footer->setText("Made with \u2764\uFE0F in Augmented Cognition Lab");
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet(
        "background: #12121a; color: #5a5a70; padding: 6px; font-size: 11px;");
    footer->setFixedHeight(28);
    mainLayout->addWidget(footer);

    setCentralWidget(central);

    setupMenus();
    setupToolbar();
    setupStatusBar();

    // Sidebar signals
    connect(m_sidebar, &StreamSidebar::streamToggled,
            this, &MainWindow::onStreamToggled);
    connect(m_sidebar, &StreamSidebar::streamSelected,
            this, &MainWindow::onStreamSelected);
    connect(m_sidebar, &StreamSidebar::channelToggled,
            this, &MainWindow::onChannelToggled);
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

    m_toolbar->addSeparator();

    m_fitAction = m_toolbar->addAction(tr("\u2922 Fit All"));
    m_fitHAction = m_toolbar->addAction(tr("\u2194 Fit H"));
    m_fitVAction = m_toolbar->addAction(tr("\u2195 Fit V"));

    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);
    connect(m_loopAction, &QAction::triggered, this, &MainWindow::onToggleLoop);
    connect(m_fitAction, &QAction::triggered, this, [this]() {
        int idx = m_streamTabs->currentIndex();
        if (idx >= 0 && idx < static_cast<int>(m_chartViews.size()) && m_chartViews[idx])
            m_chartViews[idx]->fitAxes();
    });
    connect(m_fitHAction, &QAction::triggered, this, [this]() {
        int idx = m_streamTabs->currentIndex();
        if (idx >= 0 && idx < static_cast<int>(m_chartViews.size()) && m_chartViews[idx])
            m_chartViews[idx]->fitHorizontal();
    });
    connect(m_fitVAction, &QAction::triggered, this, [this]() {
        int idx = m_streamTabs->currentIndex();
        if (idx >= 0 && idx < static_cast<int>(m_chartViews.size()) && m_chartViews[idx])
            m_chartViews[idx]->fitVertical();
    });

    m_playAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
    m_loopAction->setEnabled(false);
    m_fitAction->setEnabled(false);
    m_fitHAction->setEnabled(false);
    m_fitVAction->setEnabled(false);
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
    bool hasData = m_loader != nullptr;
    m_playAction->setEnabled(!loading && hasData);
    m_pauseAction->setEnabled(!loading && hasData);
    m_stopAction->setEnabled(!loading && hasData);
    m_loopAction->setEnabled(!loading && hasData);
    m_fitAction->setEnabled(!loading && hasData);
    m_fitHAction->setEnabled(!loading && hasData);
    m_fitVAction->setEnabled(!loading && hasData);
}

void MainWindow::loadXdfFile(const QString &filePath)
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
        m_replayEngine.reset();
    }

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

    int dataStreams = 0;
    for (int i = 0; i < m_loader->streamCount(); ++i)
        if (m_loader->stream(i).hasData()) dataStreams++;

    QString info = QString("%1 stream(s) (%2 with data) \u00B7 %3s \u00B7 %4")
                       .arg(m_loader->streamCount())
                       .arg(dataStreams)
                       .arg(m_loader->duration(), 0, 'f', 1)
                       .arg(fi.fileName());
    m_statusLabel->setText(info);

    // Sidebar
    m_sidebar->setStreams(m_loader->streams());

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

    // Build tabs with lazy chart loading — only create placeholders
    buildStreamViews();

    m_welcomeLabel->hide();
    // Show splitter (parent of sidebar + tabs)
    if (auto *splitter = qobject_cast<QSplitter *>(m_sidebar->parentWidget()))
        splitter->show();
    m_streamTabs->show();
    setLoadingState(false);

    // Build chart for the first visible tab
    if (m_streamTabs->count() > 0)
        ensureChartBuilt(0);
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

    m_tabToStream.clear();
    m_chartBuilt.clear();
    m_chartViews.clear();

    for (int i = 0; i < m_loader->streamCount(); ++i) {
        const auto &stream = m_loader->stream(i);
        if (!stream.hasData())
            continue;

        // Create a placeholder widget — chart built lazily on tab switch
        auto *placeholder = new QWidget;
        auto *layout = new QVBoxLayout(placeholder);
        auto *loadingLabel = new QLabel("Select this tab to load chart...");
        loadingLabel->setAlignment(Qt::AlignCenter);
        loadingLabel->setStyleSheet("color: #5a5a70; font-size: 14px;");
        layout->addWidget(loadingLabel);

        QString tabLabel = QString::fromStdString(stream.name);
        if (stream.channelCount > 1)
            tabLabel += QString(" (%1ch)").arg(stream.channelCount);
        m_streamTabs->addTab(placeholder, tabLabel);

        m_tabToStream.push_back(i);
        m_chartBuilt.push_back(false);
        m_chartViews.push_back(nullptr);
    }
}

void MainWindow::ensureChartBuilt(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_chartBuilt.size()))
        return;
    if (m_chartBuilt[tabIndex])
        return;

    int streamIdx = m_tabToStream[tabIndex];
    const auto &stream = m_loader->stream(streamIdx);

    m_statusLabel->setText(QString("Building chart for %1...").arg(
        QString::fromStdString(stream.name)));
    QApplication::processEvents();

    auto *chart = new StreamChartView(stream, m_loader->globalMinTime());

    // Block signals to prevent recursive onTabChanged during tab replacement
    m_streamTabs->blockSignals(true);

    QWidget *old = m_streamTabs->widget(tabIndex);
    m_streamTabs->removeTab(tabIndex);
    delete old;

    QString tabLabel = QString::fromStdString(stream.name);
    if (stream.channelCount > 1)
        tabLabel += QString(" (%1ch)").arg(stream.channelCount);
    m_streamTabs->insertTab(tabIndex, chart, tabLabel);
    m_streamTabs->setCurrentIndex(tabIndex);

    m_streamTabs->blockSignals(false);

    m_chartViews[tabIndex] = chart;
    m_chartBuilt[tabIndex] = true;

    QFileInfo fi(m_currentFilePath);
    m_statusLabel->setText(QString("Viewing: %1").arg(QString::fromStdString(stream.name)));
}

void MainWindow::onTabChanged(int index)
{
    ensureChartBuilt(index);
}

void MainWindow::onStreamToggled(int streamIndex, bool visible)
{
    // Find the tab for this stream
    for (int t = 0; t < static_cast<int>(m_tabToStream.size()); ++t) {
        if (m_tabToStream[t] == streamIndex) {
            m_streamTabs->setTabVisible(t, visible);
            break;
        }
    }
}

void MainWindow::onStreamSelected(int streamIndex)
{
    // Switch to the tab for this stream
    for (int t = 0; t < static_cast<int>(m_tabToStream.size()); ++t) {
        if (m_tabToStream[t] == streamIndex) {
            m_streamTabs->setCurrentIndex(t);
            break;
        }
    }
}

void MainWindow::onChannelToggled(int streamIndex, int channelIndex, bool visible)
{
    // Find the chart view for this stream and toggle the channel
    for (int t = 0; t < static_cast<int>(m_tabToStream.size()); ++t) {
        if (m_tabToStream[t] == streamIndex && m_chartViews[t]) {
            m_chartViews[t]->setChannelVisible(channelIndex, visible);
            break;
        }
    }
}

void MainWindow::clearViews()
{
    m_chartViews.clear();
    m_chartBuilt.clear();
    m_tabToStream.clear();
    m_streamTabs->clear();
    m_streamTabs->hide();
    m_sidebar->clear();
    if (auto *splitter = qobject_cast<QSplitter *>(m_sidebar->parentWidget()))
        splitter->hide();
    m_timeline->hide();
    m_welcomeLabel->show();
}

void MainWindow::onPlay()
{
    if (m_replayEngine) {
        m_replayEngine->play();
        m_statusLabel->setText("\u25B6 Playing...");
    }
}

void MainWindow::onPause()
{
    if (m_replayEngine) {
        m_replayEngine->pause();
        m_statusLabel->setText("\u23F8 Paused");
    }
}

void MainWindow::onStop()
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
        m_statusLabel->setText("\u23F9 Stopped");
    }
    onPlaybackPositionChanged(0.0);
}

void MainWindow::onToggleLoop()
{
    if (m_replayEngine) {
        bool on = m_loopAction->isChecked();
        m_replayEngine->setLoopEnabled(on);
        m_statusLabel->setText(on ? "\u21BB Loop enabled" : "Loop disabled");
    }
}

void MainWindow::onPlaybackPositionChanged(double seconds)
{
    m_timeline->setPlaybackPosition(seconds);

    for (size_t i = 0; i < m_chartViews.size(); ++i) {
        if (m_chartViews[i])
            m_chartViews[i]->setPlaybackCursor(seconds);
    }

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
