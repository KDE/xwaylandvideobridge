/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 */

#pragma once

#include <QQuickWindow>
#include <QObject>

class ContentsWindow : public QQuickWindow
{
    Q_OBJECT
public:
    ContentsWindow();

    //shadow super class
    void resize(const QSize &size);
private:
    void init();
    void handleResize();
};

