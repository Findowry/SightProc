# SightProc

A native GNU/Linux system monitor and process manager built with C++20 and Qt6 Widgets.

## Screenshots

- *Performance tab screenshot placeholder (`docs/screenshots/performance.png`)*
- *Applications tab screenshot placeholder (`docs/screenshots/applications.png`)*
- *Processes tab screenshot placeholder (`docs/screenshots/processes.png`)*
- *Services tab screenshot placeholder (`docs/screenshots/services.png`)*

## Design Philosophy (Zero-QSS)

SightProc intentionally avoids Qt Style Sheets (`setStyleSheet()`), hardcoded widget colors, and child-widget `WA_TranslucentBackground` attributes:

- **Native `QStyle` & `QPalette`:** All controls follow the desktop's active style engine (such as **Kvantum**, **Breeze**, or **Oxygen**) and system color palette without overrides.
- **Custom `QPainter` Charts:** Live performance charts are implemented directly as `QWidget` subclasses without external charting dependencies. Series colors, tooltips, and translucent table palettes derive dynamically from the active application palette.
- **Translucent Surfaces:** List and table views expose an adjustable opacity setting (`0%–100%`) so background blur effects can pass through cleanly when supported by the active theme.

## Features

- **Performance Tab**
  - Hardware list with mini sparklines: **CPU** (total utilization + toggleable logical core grid), **Memory & Swap/ZRAM**, **Disks / NVMe**, **Network Interfaces**, **GPUs**, **hwmon Fans**, **Batteries** (`/sys/class/power_supply`, shown when present), and **Thermal Sensors**.
  - Multi-series `hwmon` thermal sensor selection: plot specific temperature sensors together on the main chart from either the sensor table checkboxes or **+ Select Sensors**.
  - Additional per-device indicator charts separated by resizable vertical splitters (`QSplitter`) and customizable technical spec rows.
  - Optional smooth-sliding chart updates matched to the active refresh rate and floating numeric hover callouts.
  - GPU monitoring reads `/sys/class/drm` and loads `libnvidia-ml.so.1` dynamically at runtime via `dlopen` (using NVML `_v2`/`_v3` ABI structures), with sleeping dGPUs (`D3cold`) left unpolled unless explicitly enabled.

- **Applications Tab**
  - Groups desktop applications using `.desktop` entries and `cgroup` hierarchy, summing per-app **CPU %**, **Memory (RAM)**, **Shared Memory**, **Disk I/O**, **TCP Network**, **GPU %**, and **VRAM**.

- **Processes Tab**
  - Reads `/proc` across the full system process table with expandable PPID hierarchy (`ProcessTreeModel`) or flat list view, icon resolution, binary/script name restoration beyond Linux's 15-byte `comm` limit, search box, quick filters, and live `/proc` details dialog (`Details...`).
  - Uses hybrid incremental model updates (`beginInsertRows`, `beginRemoveRows`, `dataChanged`) and falls back safely to a model reset on mode switches, PID reuse (`starttime` change), or kernel reparenting.
  - Context menu supports all POSIX `SIG*` signals, custom `nice` priority changes (`-20..19`), `pkexec` elevation when required, and PID-reuse protection via `pidfd_open` / `pidfd_send_signal` plus `/proc/[pid]/stat` `starttime` verification.

- **Services Tab**
  - Lists `systemd` system and user service units via `systemctl`, supports start/stop/restart actions, and displays `journalctl` output for the selected unit.

- **Preferences & Desktop Integration**
  - Configurable refresh interval in the `500 ms–5 s` range (`500 ms`, `1 s`, `2 s`, `3 s` selectable in the UI; `500–5000 ms` supported in `~/.config/sightproc/sightproc.conf`).
  - Instant English and Turkish UI switching persisted alongside scale factors (`Ctrl` + Mouse Wheel per tab) in `~/.config/sightproc/sightproc.conf`, with a direct **Edit -> Config Directory** shortcut.
  - Status bar displays the active Qt style alongside the detected display session compositor or window manager (`<comm> (Wayland)` via `SO_PEERCRED` or `<comm> (X11)` via dynamically loaded `libX11.so.6` `_NET_WM_PID`).

> **Note on TCP Network Accounting:** The **TCP Network** column in the **Applications** and **Processes** tabs measures per-process TCP socket traffic via rootless `NETLINK_SOCK_DIAG`; UDP and QUIC traffic are excluded. Interface-level charts in the **Performance** tab report total RX/TX counters from `/sys/class/net`.

## Tested Environment

- **OS:** Arch Linux
- **Desktop:** KDE Plasma (Wayland)
- **Styles tested:** Kvantum, Breeze

## Installation

### AppImage (x86_64)

Download `SightProc-x86_64.AppImage` from the [GitHub Releases](https://github.com/Findowry/SightProc/releases) page, mark it executable, and run:

```bash
chmod +x SightProc-x86_64.AppImage
./SightProc-x86_64.AppImage
```

### Arch Linux (`PKGBUILD`)

An Arch Linux package definition is provided at [`packaging/PKGBUILD`](packaging/PKGBUILD). To build and install locally from the release tarball (or adapt `source` for a local checkout):

```bash
cd packaging
makepkg -si
```

### Build from Source (CMake + Ninja)

SightProc requires a C++20 compiler, CMake (`>= 3.22`), Ninja, and Qt 6 (`Qt6::Widgets`, plus `qt6-svg` at runtime for the SVG icon; Qt `>= 6.9` is used for `QSortFilterProxyModel::beginFilterChange`/`endFilterChange`).

Install dependencies on Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg
```

Configure and build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run directly from the build directory (the SVG icon [`data/icons/sightproc.svg`](data/icons/sightproc.svg) is embedded via Qt resources):

```bash
./build/sightproc
```

Optional system installation:

```bash
sudo cmake --install build --prefix /usr
```

## AI-Assisted Development Notice

SightProc's architecture, telemetry scope, Zero-QSS design constraints, and user experience were designed and directed by a human author. The C++20 and Qt6 Widgets source code was generated using LLM coding agents.

## License

All source code and [`data/icons/sightproc.svg`](data/icons/sightproc.svg) are released under the **MIT-0 (MIT No Attribution)** license. See [`LICENSE`](LICENSE) for the full text.
