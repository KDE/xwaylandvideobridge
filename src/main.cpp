/*
 * App to render feeds coming from xdg-desktop-portal
 * Copyright 2020 Aleix Pol Gonzalez <aleixpol@kde.org>
 * Copyright 2023 David Edmundson <davidedmundson@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xwaylandvideobridge.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <KLocalizedString>
#include <KAboutData>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");

    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    qputenv("QSG_RENDER_LOOP", "basic");
    QApplication app(argc, argv); // widgets are needed just for the SNI.
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);

    KLocalizedString::setApplicationDomain("xwaylandvideobridge");
    {
        KAboutData about("xwaylandvideobridge", i18n("Xwayland Video Bridge"), "0.1", i18n("Offer XDG Desktop Portals screencast streams to X11 apps"),
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
