/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#pragma once

#include <QObject>
#include <QQuickWindow>

class ContentsWindow : public QQuickWindow {
    Q_OBJECT
public:
    ContentsWindow();

Q_SIGNALS:
    void mirrorWindowClosed();

protected:
    void closeEvent(QCloseEvent *event) override;
};
