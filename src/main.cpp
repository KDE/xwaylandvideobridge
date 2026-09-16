/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#include "version.h"
#include "xwaylandvideobridge.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSessionManager>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

int main(int argc, char **argv)
{
    if (qgetenv("XDG_SESSION_TYPE") == "x11") {
        return 0;
    }

    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    qputenv("QSG_RENDER_LOOP", "basic");

    // QApplication since the tray menu is a QMenu and KStatusNotifierItem can fall back to QSystemTrayIcon.
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    KLocalizedString::setApplicationDomain("xwaylandvideobridge");

    KAboutData about(QStringLiteral("xwaylandvideobridge"),
                     i18n("Xwayland Video Bridge"),
                     version,
                     i18n("Offer XDG Desktop Portals screencast streams to X11 apps"),
                     KAboutLicense::GPL,
                     i18n("(C) 2022 Aleix Pol Gonzalez"));
    about.addAuthor(QStringLiteral("Aleix Pol Gonzalez"), i18n("Author"), QStringLiteral("aleixpol@kde.org"));
    about.addAuthor(QStringLiteral("David Edmundson"), i18n("Author"), QStringLiteral("davidedmundson@kde.org"));
    about.addAuthor(QStringLiteral("Hadi Chokr"), i18n("Author"), QStringLiteral("hadichokr@icloud.com"));

    KAboutData::setApplicationData(about);
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.kde.xwaylandvideobridge"), app.windowIcon()));
    KCrash::initialize();

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption autostartOption(QStringLiteral("autostart"), i18n("Start without a notification"));
    parser.addOption(autostartOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // A second bridge means a second invisible window competing for the same job.
    KDBusService service(KDBusService::Unique);

    auto *bridge = new XwaylandVideoBridge(&app);
    if (!parser.isSet(autostartOption)) {
        bridge->showRunningMessage();
    }

    QObject::connect(&service, &KDBusService::activateRequested, bridge, [bridge](const QStringList &arguments) {
        if (!arguments.contains(QLatin1String("--autostart"))) {
            bridge->showRunningMessage();
        }
    });

    return app.exec();
}
