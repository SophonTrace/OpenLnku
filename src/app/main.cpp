#include "controller.h"
#include "startup.h"
#include "speedtest.h"
#include "updater.h"
#include "proxy_query.h"
#include <windows.h>
#include <shellapi.h>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QImage>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QDir>
#include <QScreen>
#include <QPainter>
#include <QCommandLineParser>
#include <QRegularExpression>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPointer>
#include <algorithm>

namespace {
bool shellTrayRegistered() {
    struct Query { DWORD process; bool registered; } query{GetCurrentProcessId(), false};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& result = *reinterpret_cast<Query*>(data);
        DWORD process = 0; GetWindowThreadProcessId(hwnd, &process);
        if (process != result.process) return TRUE;
        wchar_t title[64]{}; GetWindowTextW(hwnd, title, 64);
        // Qt 6.8's Windows tray backend owns this message window and uses icon ID 0.
        if (wcscmp(title, L"QTrayIconMessageWindow") != 0) return TRUE;
        NOTIFYICONIDENTIFIER identifier{}; identifier.cbSize = sizeof(identifier);
        identifier.hWnd = hwnd; identifier.uID = 0;
        RECT rectangle{};
        // S_FALSE is also a registered icon (Windows can put it in the overflow area).
        result.registered = SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rectangle));
        return result.registered ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.registered;
}
QIcon statusIcon(const QColor& color) {
    QPixmap pixmap(64, 64); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.drawPixmap(2, 2, 58, 58, QPixmap(":/branding/app.png"));
    painter.setPen(QPen(Qt::white, 3)); painter.setBrush(color); painter.drawEllipse(QRectF(42, 42, 19, 19));
    return QIcon(pixmap);
}
}
int main(int argc, char* argv[]) {
    const int proxyQuery = runInternalProxyQuery(argc, argv);
    if (proxyQuery >= 0) return proxyQuery;
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("OpenNex");
    QCoreApplication::setApplicationName("OpenNex");
    QGuiApplication::setApplicationDisplayName("OpenNex");
    QCoreApplication::setApplicationVersion(OPENNEX_VERSION);
    const auto arguments = app.arguments();
    QCommandLineParser parser; parser.setApplicationDescription("OpenNex"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{"live", "Network authentication"}, {"demo", "Offline demonstration"}, {"tray", "Start in system tray"},
        {"capture", "Capture offline UI", "file"}, {"demo-connect", "Connect offline simulation"},
        {"page", "Preview page", "index"}, {"settings-file", "Isolated offline settings", "file"},
        {"test-identity", "Isolated offline instance identity", "name"}, {"scale", "Preview window size", "size"},
        {"quit-after", "Offline test lifetime", "milliseconds"}, {"verify-tray", "Offline tray acceptance"},
        {"verify-ui", "Offline license and close acceptance"}, {"demo-license", "Preview in-app license"},
        {"verify-tray-failure", "Offline tray failure recovery acceptance"}});
    parser.process(app);
    const auto mode = startupMode(arguments);
    if (mode == StartupMode::Invalid) { qCritical("Invalid mode: offline options require --demo."); return 2; }
    const bool demo = mode == StartupMode::Demo, capture = parser.isSet("capture");
    const QString testIdentity = parser.value("test-identity");
    if (!testIdentity.isEmpty() && !QRegularExpression("^OpenNexAcceptance[A-Za-z0-9_-]*$").match(testIdentity).hasMatch()) return 2;
    const QString identity = testIdentity.isEmpty() ? QStringLiteral("OpenNex") : testIdentity;
    const QString installerMutex = "Local\\" + identity + ".Installer";
    if (HANDLE installing = OpenMutexW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(installerMutex.utf16()))) {
        CloseHandle(installing); return 4;
    }
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!parser.value("settings-file").isEmpty()) path = QFileInfo(parser.value("settings-file")).absolutePath();
    QDir().mkpath(path);
    QLockFile lock(path + "/" + identity + (demo ? "-demo.lock" : "-live.lock"));
    const QString mutexName = "Local\\" + identity + ".App." + (demo ? "Demo" : "Live");
    HANDLE instanceMutex = nullptr;
    if (!capture) {
        instanceMutex = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
        if (!instanceMutex) return 3;
        if (GetLastError() == ERROR_ALREADY_EXISTS || !lock.tryLock(0)) {
            const auto hwnd = FindWindowW(nullptr, demo ? L"OpenNex · 离线演示" : L"OpenNex");
            if (hwnd) PostMessageW(hwnd, RegisterWindowMessageW(L"OpenNex.ShowWindow"), 0, 0);
            CloseHandle(instanceMutex); return 3;
        }
    }
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
        const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
        QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
        QFontDatabase::addApplicationFont(fonts + "msyhbd.ttc");
    }
    QQuickStyle::setStyle("Basic"); app.setFont(QFont("Microsoft YaHei UI", 10));
    const QIcon icon(":/branding/app.png"); app.setWindowIcon(icon);
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable() && !capture;
    Controller controller(demo, nullptr, parser.value("settings-file"));
    SpeedTest speed(demo);
    Updater updates(demo);
    QObject::connect(&updates, &Updater::installationStarted, &controller, &Controller::requestQuit);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("client", &controller);
    engine.rootContext()->setContextProperty("speed", &speed);
    engine.rootContext()->setContextProperty("updates", &updates);
    engine.rootContext()->setContextProperty("trayAvailable", trayAvailable);
    engine.loadFromModule("OpenNex", "Main");
    if (engine.rootObjects().isEmpty()) { if (instanceMutex) CloseHandle(instanceMutex); return 1; }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    if (!window) { if (instanceMutex) CloseHandle(instanceMutex); return 1; }
    window->setIcon(icon);
    std::shared_ptr<QSettings> windowSettings;
    if (!parser.value("settings-file").isEmpty()) windowSettings = std::make_shared<QSettings>(parser.value("settings-file"), QSettings::IniFormat);
    else if (!demo) windowSettings = std::make_shared<QSettings>("OpenNex", "OpenNex");
    if (windowSettings && windowSettings->value("windowLayoutVersion").toString() == "1.1" && windowSettings->contains("windowGeometry")) {
        const auto saved = windowSettings->value("windowGeometry").toRect();
        for (const auto* screen : QGuiApplication::screens()) {
            const auto available = screen->availableGeometry();
            if (!saved.intersects(available)) continue;
            const QSize size(std::min(std::max(saved.width(), window->minimumWidth()), available.width()),
                             std::min(std::max(saved.height(), window->minimumHeight()), available.height()));
            const QPoint position(std::clamp(saved.x(), available.left(), available.right() - size.width() + 1),
                                  std::clamp(saved.y(), available.top(), available.bottom() - size.height() + 1));
            window->setGeometry(QRect(position, size)); break;
        }
    }
    const QPointer<QQuickWindow> geometryWindow(window);
    const auto saveGeometry = [windowSettings, geometryWindow] {
        if (windowSettings && geometryWindow && geometryWindow->windowState() == Qt::WindowNoState) {
            windowSettings->setValue("windowGeometry", geometryWindow->geometry());
            windowSettings->setValue("windowLayoutVersion", "1.1"); windowSettings->sync();
        }
    };
    QObject::connect(window, &QWindow::visibleChanged, &app, [saveGeometry](bool visible) { if (!visible) saveGeometry(); });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, saveGeometry);
    const auto showWindow = [window] { window->setProperty("page", 0); window->showNormal(); window->raise(); window->requestActivate(); };
    QObject::connect(&controller, &Controller::showWindowRequested, window, showWindow);
    if (parser.isSet("page")) window->setProperty("page", parser.value("page").toInt());
    if (demo && capture && parser.value("page") == "1") speed.preview();
    if (parser.isSet("scale")) {
        const auto dimensions = parser.value("scale").split('x');
        if (dimensions.size() == 2) window->resize(dimensions[0].toInt(), dimensions[1].toInt());
    }
    QSystemTrayIcon tray(icon);
    QMenu menu;
    auto* openAction = menu.addAction(QStringLiteral("打开 OpenNex"), window, showWindow);
    auto* connectAction = menu.addAction(QStringLiteral("连接"), &controller, &Controller::connectSaved);
    auto* disconnectAction = menu.addAction(QStringLiteral("断开连接"), &controller, &Controller::disconnectNow);
    auto* retryAction = menu.addAction(QStringLiteral("立即重试"), &controller, &Controller::retryNow);
    menu.addSeparator();
    auto* reconnectAction = menu.addAction(QStringLiteral("自动重连")); reconnectAction->setCheckable(true);
    QObject::connect(reconnectAction, &QAction::triggered, &controller, &Controller::setAutoReconnect);
    menu.addSeparator(); menu.addAction(QStringLiteral("退出 OpenNex"), &controller, &Controller::requestQuit);
    tray.setContextMenu(&menu);
    QString previousIconKey;
    const auto updateTray = [&] {
        connectAction->setEnabled(!controller.busy());
        disconnectAction->setEnabled(controller.active());
        retryAction->setVisible(controller.retrySeconds() > 0);
        reconnectAction->setChecked(controller.autoReconnect());
        const QString key = controller.online() ? "online" : controller.busy() ? "active" : "idle";
        if (key != previousIconKey) {
            tray.setIcon(statusIcon(controller.online() ? QColor("#28A876") : controller.busy() ? QColor("#E4A43C") : QColor("#8994A7")));
            previousIconKey = key;
        }
        tray.setToolTip(QStringLiteral("OpenNex %1\n%2\n%3 · %4").arg(QCoreApplication::applicationVersion(), controller.status(), controller.adapterLabel(), controller.uptime()));
    };
    QObject::connect(&controller, &Controller::changed, &tray, updateTray); updateTray();
    QObject::connect(&controller, &Controller::notificationRequested, &tray, [&](const QString& title, const QString& message) {
        if (trayAvailable) tray.showMessage(title, message, QSystemTrayIcon::Information, 7000);
    });
    QObject::connect(&tray, &QSystemTrayIcon::messageClicked, window, showWindow);
    QObject::connect(&controller, &Controller::readyToQuit, &app, [&] { window->setProperty("forceClose", true); app.quit(); });
    app.setQuitOnLastWindowClosed(!trayAvailable);
    if (trayAvailable) tray.show();
    QObject::connect(&tray, &QSystemTrayIcon::activated, window, [showWindow](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    const auto trayRegistered = [&tray, trayAvailable, demo, &parser] {
        if (demo && parser.isSet("verify-tray-failure")) return false;
        // Query the Shell directly: Qt geometry() also filters out overflow icons.
        return trayAvailable && tray.isVisible() && shellTrayRegistered();
    };
    QObject::connect(&controller, &Controller::closeWindowRequested, window, [&] {
        if (!controller.closeToTray()) { controller.requestQuit(); return; }
        if (trayRegistered()) { window->hide(); return; }
        if (trayAvailable) { tray.hide(); tray.show(); }
        QTimer::singleShot(350, window, [&] {
            if (trayRegistered()) window->hide();
            else { showWindow(); controller.reportTrayFailure(); }
        });
    });
    if (parser.isSet("tray")) QTimer::singleShot(500, window, [&] {
        if (trayRegistered()) window->hide();
        else { showWindow(); controller.reportTrayFailure(); }
    });
    QTimer trayWatchdog;
    trayWatchdog.setInterval(5000);
    QObject::connect(&trayWatchdog, &QTimer::timeout, window, [&] {
        if (capture || window->isVisible() || trayRegistered()) return;
        if (trayAvailable) { tray.hide(); tray.show(); }
        QTimer::singleShot(700, window, [&] {
            if (!trayRegistered() && !window->isVisible()) { showWindow(); controller.reportTrayFailure(); }
        });
    });
    trayWatchdog.start();
    if (demo) qInfo("offline mode: trayAvailable=%d", trayAvailable);
    if (demo && parser.isSet("demo-license")) QTimer::singleShot(0, &controller, &Controller::openLicense);
    if (demo && parser.isSet("verify-ui")) {
        QTimer::singleShot(350, &controller, &Controller::openLicense);
        QTimer::singleShot(700, &app, [&] {
            auto* dialog = window->findChild<QObject*>("licenseDialog");
            if (!dialog || !dialog->property("visible").toBool() || !controller.licenseText().contains("GNU AFFERO GENERAL PUBLIC LICENSE")) {
                app.exit(10); return;
            }
            QMetaObject::invokeMethod(dialog, "close");
            controller.setCloseToTray(false);
            qInfo("offline UI acceptance: license=1 eventLoopResponsive=1 closeToTrayOff=1");
            window->close();
        });
    }
    bool trayFailureReported = false;
    if (demo && parser.isSet("verify-tray-failure")) {
        controller.setCloseToTray(true);
        QObject::connect(&controller, &Controller::errorRequested, &app, [&](const QString&) { trayFailureReported = true; });
        QTimer::singleShot(350, window, [&] { window->close(); });
        QTimer::singleShot(950, &app, [&] {
            if (!window->isVisible() || !trayFailureReported) { app.exit(11); return; }
            qInfo("offline tray failure acceptance: windowVisible=1 errorReported=1");
            controller.requestQuit();
        });
    }
    if (demo && parser.isSet("quit-after")) {
        bool ok = false; const int lifetime = parser.value("quit-after").toInt(&ok);
        if (!ok || lifetime < 1 || lifetime > 60000) { if (instanceMutex) CloseHandle(instanceMutex); return 2; }
        QTimer::singleShot(lifetime, &controller, &Controller::requestQuit);
    }
    if (demo && parser.isSet("verify-tray")) {
        controller.setCloseToTray(true);
        if (!trayAvailable) { if (instanceMutex) CloseHandle(instanceMutex); return 5; }
        QTimer::singleShot(500, &app, [&] {
            qInfo("native tray probe: integrityRID=%lu qtVisible=%d shellRegistered=%d", jlu::processIntegrity(), tray.isVisible(), shellTrayRegistered());
            window->show(); window->close();
            if (window->isVisible() || !trayRegistered()) { app.exit(6); return; }
            connectAction->trigger();
        });
        QTimer::singleShot(3000, &app, [&] {
            if (!controller.online() || connectAction->isEnabled() || !disconnectAction->isEnabled()) { app.exit(7); return; }
            disconnectAction->trigger();
        });
        QTimer::singleShot(4200, &app, [&] {
            if (controller.online() || controller.busy() || !connectAction->isEnabled()) { app.exit(8); return; }
            openAction->trigger();
            if (!window->isVisible()) { app.exit(9); return; }
            qInfo("native tray acceptance: hide=1 connect=1 disconnect=1 restore=1 demo=1");
            controller.requestQuit();
        });
    }
    if (capture) {
        const QString output = parser.value("capture");
        if (parser.isSet("demo-connect")) controller.connectNow("example001", "demo1234", false);
        QTimer::singleShot(2600, &app, [&] {
            const bool saved = window->grabWindow().save(output);
            qInfo("capture saved=%d demo=%d simulationComplete=%d heartbeats=%llu", saved, demo, controller.online(), controller.heartbeats());
            app.exit(saved ? 0 : 4);
        });
    }
    const int result = app.exec();
    if (instanceMutex) CloseHandle(instanceMutex);
    return result;
}
