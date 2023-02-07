/*
    SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

import QtQuick 2.1
import QtQuick.Window 2.15
import org.kde.pipewire 0.1 as PipeWire

Window
{
    property alias fd: sourceItem.fd
    property alias nodeId: sourceItem.nodeId

    PipeWire.PipeWireSourceItem {
        id: sourceItem
        anchors.fill: parent
    }
}
