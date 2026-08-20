#! /usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
# SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>

$EXTRACTRC `find src -name \*.ui -o -name \*.rc -o -name \*.kcfg` >> rc.cpp
$XGETTEXT `find src -name \*.cpp -o -name \*.h -o -name \*.qml` rc.cpp -o $podir/xwaylandvideobridge.pot
rm -f rc.cpp
