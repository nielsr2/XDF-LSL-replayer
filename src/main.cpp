#include <QApplication>
#include <QSplashScreen>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QFont>
#include "MainWindow.h"

// For static Qt builds, import the platform plugin at compile time
#ifdef QT_STATICPLUGIN
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

static QPixmap createSplashPixmap()
{
    QPixmap pix(600, 360);
    pix.fill(QColor(24, 24, 32));

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Gradient accent bar
    QLinearGradient grad(0, 0, 600, 0);
    grad.setColorAt(0.0, QColor(80, 140, 255));
    grad.setColorAt(1.0, QColor(160, 80, 255));
    p.fillRect(0, 0, 600, 6, grad);

    // App title
    QFont titleFont("Segoe UI", 32, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(QColor(240, 240, 255));
    p.drawText(pix.rect().adjusted(0, 60, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               "XDF LSL Replayer");

    // Subtitle
    QFont subFont("Segoe UI", 14);
    p.setFont(subFont);
    p.setPen(QColor(140, 150, 180));
    p.drawText(pix.rect().adjusted(0, 120, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               "Record  \u00B7  Visualize  \u00B7  Replay");

    // Wave decoration
    p.setPen(QPen(QColor(80, 140, 255, 80), 2));
    for (int wave = 0; wave < 3; ++wave) {
        QPainterPath path;
        double yBase = 200 + wave * 25;
        path.moveTo(40, yBase);
        for (int x = 40; x <= 560; x += 2) {
            double y = yBase + 12.0 * std::sin((x + wave * 40) * 0.04);
            path.lineTo(x, y);
        }
        p.drawPath(path);
    }

    // Footer
    QFont footFont("Segoe UI", 10);
    p.setFont(footFont);
    p.setPen(QColor(120, 130, 160));
    p.drawText(pix.rect().adjusted(0, 0, 0, -20), Qt::AlignHCenter | Qt::AlignBottom,
               "Made with \u2764 in Augmented Cognition Lab");

    // Version
    QFont verFont("Segoe UI", 9);
    p.setFont(verFont);
    p.setPen(QColor(80, 90, 110));
    p.drawText(pix.rect().adjusted(0, 0, -15, -20), Qt::AlignRight | Qt::AlignBottom,
               "v1.0.0");

    p.end();
    return pix;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("XDF LSL Replayer");
    app.setApplicationVersion("1.0.0");

    // Apply dark fusion style
    app.setStyle("Fusion");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 38));
    darkPalette.setColor(QPalette::WindowText, QColor(220, 220, 230));
    darkPalette.setColor(QPalette::Base, QColor(22, 22, 30));
    darkPalette.setColor(QPalette::AlternateBase, QColor(36, 36, 46));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(50, 50, 60));
    darkPalette.setColor(QPalette::ToolTipText, QColor(220, 220, 230));
    darkPalette.setColor(QPalette::Text, QColor(220, 220, 230));
    darkPalette.setColor(QPalette::Button, QColor(40, 40, 52));
    darkPalette.setColor(QPalette::ButtonText, QColor(220, 220, 230));
    darkPalette.setColor(QPalette::BrightText, QColor(255, 80, 80));
    darkPalette.setColor(QPalette::Link, QColor(100, 160, 255));
    darkPalette.setColor(QPalette::Highlight, QColor(80, 140, 255));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::PlaceholderText, QColor(100, 100, 120));
    app.setPalette(darkPalette);

    app.setStyleSheet(
        "QToolBar { border: none; background: #1e1e26; spacing: 6px; padding: 4px; }"
        "QToolBar QToolButton { color: #dcdce6; padding: 6px 12px; border-radius: 4px; }"
        "QToolBar QToolButton:hover { background: #2a2a3a; }"
        "QToolBar QToolButton:checked { background: #3a4a6a; }"
        "QStatusBar { background: #16161e; color: #8890a0; }"
        "QTabWidget::pane { border: 1px solid #2a2a3a; }"
        "QTabBar::tab { background: #1e1e26; color: #8890a0; padding: 8px 16px; "
        "  border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #2a2a3a; color: #dcdce6; }"
        "QTabBar::tab:hover { background: #252534; }"
        "QMenuBar { background: #1e1e26; color: #dcdce6; }"
        "QMenuBar::item:selected { background: #2a2a3a; }"
        "QMenu { background: #1e1e26; color: #dcdce6; border: 1px solid #2a2a3a; }"
        "QMenu::item:selected { background: #3a4a6a; }"
        "QPushButton { background: #2a2a3a; color: #dcdce6; border: 1px solid #3a3a4a; "
        "  border-radius: 4px; padding: 5px 14px; }"
        "QPushButton:hover { background: #3a3a4a; }"
        "QProgressBar { border: 1px solid #2a2a3a; border-radius: 4px; text-align: center; "
        "  background: #16161e; color: #dcdce6; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "  stop:0 #508cff, stop:1 #a050ff); border-radius: 3px; }"
    );

    // Splash screen
    QSplashScreen splash(createSplashPixmap());
    splash.show();
    app.processEvents();

    MainWindow window;

    QTimer::singleShot(5000, [&]() {
        splash.close();
        window.show();
    });

    return app.exec();
}
