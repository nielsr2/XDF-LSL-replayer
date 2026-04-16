#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QTimer>
#include <memory>

class XdfLoader;
class LslReplayEngine;
class StreamChartView;
class TimelineWidget;

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

private:
    void loadXdfFile(const QString &filePath);
    void setupMenus();
    void setupToolbar();
    void setupStatusBar();
    void buildStreamViews();
    void clearViews();

    std::unique_ptr<XdfLoader> m_loader;
    std::unique_ptr<LslReplayEngine> m_replayEngine;

    QTabWidget *m_streamTabs = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QToolBar *m_toolbar = nullptr;

    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_loopAction = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_timeLabel = nullptr;

    std::vector<StreamChartView *> m_chartViews;
};

#endif // MAINWINDOW_H
