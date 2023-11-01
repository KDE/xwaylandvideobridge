#! /usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>

$EXTRACTRC `find . -name \*.ui -o -name \*.rc -o -name \*.kcfg` >> rc.cpp
$XGETTEXT `find . -name \*.cpp` -o $podir/xwaylandvideobridge.pot
rm -f rc.cpp
