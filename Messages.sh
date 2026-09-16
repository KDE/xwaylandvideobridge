#! /usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
# SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>

$XGETTEXT `find src -name \*.cpp -o -name \*.h` -o $podir/xwaylandvideobridge.pot
