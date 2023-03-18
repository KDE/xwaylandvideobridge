# XWayland Video Bridge

# About

By design, X11 applications can't access window or screen contents for wayland clients. This is fine in princple, but it breaks screen sharing in tools like Discord, MS Teams, Skype, etc and more.

This tool allows us to share specific windows to X11 clients, but within the control of the user at all times.

# How to use

Launch pwbypass it will run in the background. Next time you try to share a window a prompt will appear.
The previously selected window should now be available for sharing. The title will always be "Wayland to X11 bridge" no matter what window is selected.

The system tray icon provides finer control.

# Compilation

Right now this requires a specific hash of kpipewire until we bump this to Qt6

# Use outside Plasma

This should work on any desktop that supports the Xdg desktop portals and pipewire streaming and have a working system tray.

# Future

Ideally this should be more automatic, but this tool aims purely to serve as a stop-gap whilst we wait for these clients to get native wayland support and for the surrounding wayland protocols to be better. How much more it gets developed depends on feedback and how the surrounding ecosystem evolves.

