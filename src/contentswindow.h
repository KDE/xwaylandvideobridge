/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#pragma once

#include <QQuickWindow>

class QScreen;

class ContentsWindow : public QQuickWindow
{
    Q_OBJECT
public:
    ContentsWindow();

    void showForStream(const QSize &streamSize);
    void goIdle();

Q_SIGNALS:
    void mirrorWindowClosed();
    void windowIdChanged(WId windowId);

protected:
    void closeEvent(QCloseEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void trackScreen(QScreen *newScreen);
    void syncWindowId();
    void applyGeometry();
    void applyWindowState();
    void clearInputRegion();
    QSize constrainToScreen(const QSize &size) const;

    QMetaObject::Connection m_screenConnection;
    QSize m_streamSize;
    WId m_windowId = 0;
    bool m_exposed = false;
};
