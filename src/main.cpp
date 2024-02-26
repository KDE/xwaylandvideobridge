/*
 * App to render feeds coming from xdg-desktop-portal
 *
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 */

#include "xwaylandvideobridge.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSessionManager>
#include <QIcon>
#include <KLocalizedString>
#include <KAboutData>

int main(int argc, char **argv)
{
    if (qgetenv("XDG_SESSION_TYPE") == "x11") {
        return 0;
    }
    qputenv("QT_QPA_PLATFORM", "xcb");

    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    qputenv("QSG_RENDER_LOOP", "basic");
    QApplication app(argc, argv); // widgets are needed just for the SNI.
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);

     auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    KLocalizedString::setApplicationDomain("xwaylandvideobridge");
    {
        KAboutData about("xwaylandvideobridge", i18n("Xwayland Video Bridge"), version, i18n("Offer XDG Desktop Portals screencast streams to X11 apps"),
                         KAboutLicense::GPL, i18n("(C) 2022 Aleix Pol Gonzalez"));

        about.addAuthor("Aleix Pol Gonzalez", i18n("Author"), "aleixpol@kde.org" );
        about.addAuthor("David Edmundson", i18n("Author"), "davidedmundson@kde.org" );

        KAboutData::setApplicationData(about);
        QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("xwaylandvideobridge"), app.windowIcon()));

        QCommandLineParser parser;
        about.setupCommandLine(&parser);
        parser.process(app);
        about.processCommandLine(&parser);

        new XwaylandVideoBridge(&app);
    }
    return app.exec();
}
