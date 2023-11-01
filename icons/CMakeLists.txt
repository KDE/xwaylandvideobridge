# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>

ecm_install_icons(
    ICONS
        sc-apps-xwaylandvideobridge.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

ecm_install_icons(ICONS ${xwaylandvideobridge_ICONS} DESTINATION ${KDE_INSTALL_ICONDIR} THEME hicolor)
