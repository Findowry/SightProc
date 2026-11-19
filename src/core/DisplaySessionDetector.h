#pragma once

#include <QString>

namespace sysmon {

// Detects active compositor (Wayland SO_PEERCRED -> /proc/<pid>/comm) or
// window manager (X11 _NET_SUPPORTING_WM_CHECK + _NET_WM_PID -> /proc/<pid>/comm)
// with zero hardcoded compositor or WM names.
QString detectActiveDisplaySession(const QString& qtPlatformName);

} // namespace sysmon
