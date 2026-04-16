#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QProgressBar>
#include <QTimer>
#include <QThread>
#include <memory>

class XdfLoader;
class LslReplayEngine;
class StreamChartView;
class TimelineWidget;

// Worker for background file loading
class XdfLoadWorker : public QObject
{
    Q_OBJECT
public:
    explicit XdfLoadWorker(const QString &filePath) : m_filePath(filePath) {}
public slots:
    void process();
signals:
    void finished(XdfLoader *loader);
    void error(const QString &msg);
    void progress(const QString &status);
private:
    QString m_filePath;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFile();
    void onPlay();
    void onPause();
    void onStop();
    void onToggleLoop();
    void onPlaybackPositionChanged(double seconds);
    void onLoopRegionChanged(double startSec, double endSec);
    void onFileLoaded(XdfLoader *loader);
    void onFileLoadError(const QString &msg);
    void onLoadProgress(const QString &status);

private:
    void loadXdfFile(const QString &filePath);
    void setupMenus();
    void setupToolbar();
    void setupStatusBar();
    void setupFooter();
    void buildStreamViews();
    void clearViews();
    void setLoadingState(bool loading);

    std::unique_ptr<XdfLoader> m_loader;
    std::unique_ptr<LslReplayEngine> m_replayEngine;

    QTabWidget *m_streamTabs = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QToolBar *m_toolbar = nullptr;
    QWidget *m_centralStack = nullptr;
    QLabel *m_welcomeLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;

    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_loopAction = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_timeLabel = nullptr;

    QThread *m_loadThread = nullptr;
    QString m_currentFilePath;

    std::vector<StreamChartView *> m_chartViews;
};

#endif // MAINWINDOW_H
