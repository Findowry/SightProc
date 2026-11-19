#include "DisplaySessionDetector.h"
#include "FormatUtils.h"

#include <QByteArray>
#include <QFile>

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace sysmon {
namespace {

QString readProcComm(qint64 pid) {
    if (pid <= 0) {
        return {};
    }
    const QString path = QStringLiteral("/proc/%1/comm").arg(pid);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QByteArray raw = f.readAll().trimmed();
    return QString::fromLocal8Bit(raw);
}

QString detectWaylandCompositorComm() {
    QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    if (waylandDisplay.isEmpty()) {
        waylandDisplay = QByteArrayLiteral("wayland-0");
    }

    QByteArray socketPath;
    if (waylandDisplay.startsWith('/')) {
        socketPath = waylandDisplay;
    } else {
        const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
        if (runtimeDir.isEmpty()) {
            return {};
        }
        socketPath = runtimeDir + '/' + waylandDisplay;
    }

    struct sockaddr_un addr {};
    if (static_cast<std::size_t>(socketPath.size()) >= sizeof(addr.sun_path)) {
        return {};
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return {};
    }

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socketPath.constData(), static_cast<std::size_t>(socketPath.size()) + 1);

    QString comm;
    if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        struct ucred cred {};
        socklen_t len = sizeof(cred);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.pid > 0) {
            comm = readProcComm(static_cast<qint64>(cred.pid));
        }
    }

    ::close(fd);
    return comm;
}

extern "C" {
struct _XDisplay;
using X11Display = struct _XDisplay;
using X11Window = unsigned long;
using X11Atom = unsigned long;
using X11Bool = int;
using X11ErrorHandler = int (*)(X11Display*, void*);
}

static int silentX11ErrorHandler(X11Display*, void*) {
    return 0;
}

template <typename Fn>
bool loadX11Sym(void* lib, const char* name, Fn& out) {
    void* sym = ::dlsym(lib, name);
    if (!sym) {
        return false;
    }
    std::memcpy(&out, &sym, sizeof(out));
    return true;
}

QString detectX11WmComm() {
    if (qgetenv("DISPLAY").isEmpty()) {
        return {};
    }

    void* lib = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        return {};
    }

    X11Display* (*pXOpenDisplay)(const char*) = nullptr;
    int (*pXCloseDisplay)(X11Display*) = nullptr;
    X11Atom (*pXInternAtom)(X11Display*, const char*, X11Bool) = nullptr;
    X11Window (*pXDefaultRootWindow)(X11Display*) = nullptr;
    int (*pXGetWindowProperty)(X11Display*, X11Window, X11Atom, long, long, X11Bool,
                               X11Atom, X11Atom*, int*, unsigned long*, unsigned long*,
                               unsigned char**) = nullptr;
    int (*pXFree)(void*) = nullptr;
    X11ErrorHandler (*pXSetErrorHandler)(X11ErrorHandler) = nullptr;

    const bool ok = loadX11Sym(lib, "XOpenDisplay", pXOpenDisplay)
        && loadX11Sym(lib, "XCloseDisplay", pXCloseDisplay)
        && loadX11Sym(lib, "XInternAtom", pXInternAtom)
        && loadX11Sym(lib, "XDefaultRootWindow", pXDefaultRootWindow)
        && loadX11Sym(lib, "XGetWindowProperty", pXGetWindowProperty)
        && loadX11Sym(lib, "XFree", pXFree)
        && loadX11Sym(lib, "XSetErrorHandler", pXSetErrorHandler);

    if (!ok) {
        ::dlclose(lib);
        return {};
    }

    X11Display* dpy = pXOpenDisplay(nullptr);
    if (!dpy) {
        ::dlclose(lib);
        return {};
    }

    X11ErrorHandler prevHandler = pXSetErrorHandler(&silentX11ErrorHandler);
    QString comm;

    const X11Window root = pXDefaultRootWindow(dpy);
    const X11Atom netSupporting = pXInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", 1);
    const X11Atom netWmPid = pXInternAtom(dpy, "_NET_WM_PID", 1);
    constexpr X11Atom anyPropType = 0L;

    if (netSupporting != 0 && netWmPid != 0) {
        X11Atom actualType = 0;
        int actualFormat = 0;
        unsigned long nitems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* prop = nullptr;

        if (pXGetWindowProperty(dpy, root, netSupporting, 0, 1, 0, anyPropType,
                                &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == 0
            && prop != nullptr) {
            X11Window wmWin = 0;
            if (nitems >= 1 && actualFormat == 32) {
                unsigned long rawWin = 0;
                std::memcpy(&rawWin, prop, sizeof(rawWin));
                wmWin = rawWin;
            }
            pXFree(prop);
            prop = nullptr;

            if (wmWin != 0) {
                actualType = 0;
                actualFormat = 0;
                nitems = 0;
                bytesAfter = 0;
                if (pXGetWindowProperty(dpy, wmWin, netWmPid, 0, 1, 0, anyPropType,
                                        &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == 0
                    && prop != nullptr) {
                    if (nitems >= 1 && actualFormat == 32) {
                        unsigned long rawPid = 0;
                        std::memcpy(&rawPid, prop, sizeof(rawPid));
                        comm = readProcComm(static_cast<qint64>(rawPid));
                    }
                    pXFree(prop);
                }
            }
        }
    }

    pXSetErrorHandler(prevHandler);
    pXCloseDisplay(dpy);
    ::dlclose(lib);
    return comm;
}

} // namespace

QString detectActiveDisplaySession(const QString& qtPlatformName) {
    const QString platformLower = qtPlatformName.toLower();
    const bool isWayland = platformLower.startsWith(QStringLiteral("wayland"))
        || (platformLower.isEmpty() && qEnvironmentVariableIsSet("WAYLAND_DISPLAY"));

    if (isWayland) {
        const QString comm = detectWaylandCompositorComm();
        return comm.isEmpty()
            ? QStringLiteral("Wayland")
            : QStringLiteral("%1 (Wayland)").arg(comm);
    }

    const bool isX11 = platformLower == QStringLiteral("xcb")
        || qEnvironmentVariableIsSet("DISPLAY");
    if (isX11) {
        const QString comm = detectX11WmComm();
        return comm.isEmpty()
            ? QStringLiteral("X11")
            : QStringLiteral("%1 (X11)").arg(comm);
    }

    return qtPlatformName.isEmpty() ? QStringLiteral("Unknown") : qtPlatformName;
}

} // namespace sysmon
