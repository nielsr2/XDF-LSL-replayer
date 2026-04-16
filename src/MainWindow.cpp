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
#include <QStyle>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("XDF LSL Replayer");
    resize(1200, 700);
    setAcceptDrops(true);

    // Central widget layout
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);

    m_streamTabs = new QTabWidget;
    layout->addWidget(m_streamTabs, 1);

    m_timeline = new TimelineWidget;
    layout->addWidget(m_timeline);

    setCentralWidget(central);

    setupMenus();
    setupToolbar();
    setupStatusBar();

    // Timeline signals
    connect(m_timeline, &TimelineWidget::loopRegionChanged,
            this, &MainWindow::onLoopRegionChanged);
}

MainWindow::~MainWindow()
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
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

    m_playAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Play"));
    m_pauseAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPause), tr("Pause"));
    m_stopAction = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"));

    m_toolbar->addSeparator();

    m_loopAction = m_toolbar->addAction(tr("Loop"));
    m_loopAction->setCheckable(true);
    m_loopAction->setChecked(false);

    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);
    connect(m_loopAction, &QAction::triggered, this, &MainWindow::onToggleLoop);

    // Disable until a file is loaded
    m_playAction->setEnabled(false);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
    m_loopAction->setEnabled(false);
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel(tr("No file loaded"));
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

void MainWindow::loadXdfFile(const QString &filePath)
{
    // Stop any existing replay
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
        m_replayEngine.reset();
    }

    clearViews();

    m_loader = std::make_unique<XdfLoader>();
    if (!m_loader->load(filePath)) {
        QMessageBox::warning(this, tr("Error"), m_loader->errorString());
        m_loader.reset();
        return;
    }

    setWindowTitle(QString("XDF LSL Replayer - %1").arg(filePath));
    m_statusLabel->setText(QString("%1 stream(s), duration: %2s")
                               .arg(m_loader->streamCount())
                               .arg(m_loader->duration(), 0, 'f', 2));

    // Set up timeline
    m_timeline->setDuration(m_loader->duration());

    // Create replay engine
    m_replayEngine = std::make_unique<LslReplayEngine>();
    m_replayEngine->setStreams(&m_loader->streams(),
                                m_loader->globalMinTime(),
                                m_loader->globalMaxTime());

    connect(m_replayEngine.get(), &LslReplayEngine::playbackPositionChanged,
            this, &MainWindow::onPlaybackPositionChanged, Qt::QueuedConnection);
    connect(m_replayEngine.get(), &LslReplayEngine::playbackFinished,
            this, &MainWindow::onStop, Qt::QueuedConnection);

    // Build views
    buildStreamViews();

    // Enable controls
    m_playAction->setEnabled(true);
    m_pauseAction->setEnabled(true);
    m_stopAction->setEnabled(true);
    m_loopAction->setEnabled(true);
}

void MainWindow::buildStreamViews()
{
    if (!m_loader)
        return;

    for (int i = 0; i < m_loader->streamCount(); ++i) {
        const auto &stream = m_loader->stream(i);
        auto *chart = new StreamChartView(stream, m_loader->globalMinTime());
        m_streamTabs->addTab(chart, QString::fromStdString(stream.name));
        m_chartViews.push_back(chart);
    }
}

void MainWindow::clearViews()
{
    m_chartViews.clear();
    m_streamTabs->clear();
}

void MainWindow::onPlay()
{
    if (m_replayEngine)
        m_replayEngine->play();
}

void MainWindow::onPause()
{
    if (m_replayEngine)
        m_replayEngine->pause();
}

void MainWindow::onStop()
{
    if (m_replayEngine) {
        m_replayEngine->stop();
        m_replayEngine->wait();
    }
    onPlaybackPositionChanged(0.0);
}

void MainWindow::onToggleLoop()
{
    if (m_replayEngine)
        m_replayEngine->setLoopEnabled(m_loopAction->isChecked());
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
