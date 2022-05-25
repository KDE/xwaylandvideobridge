ecm_install_icons(
    ICONS
        sc-apps-screenrecord.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

ecm_install_icons(ICONS ${screenrecord_ICONS} DESTINATION ${KDE_INSTALL_ICONDIR} THEME hicolor)
