ecm_install_icons(
    ICONS
        sc-apps-pwbypass.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

ecm_install_icons(ICONS ${pwbypass_ICONS} DESTINATION ${KDE_INSTALL_ICONDIR} THEME hicolor)
