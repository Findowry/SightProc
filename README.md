# SightProc

**Qt6 Widgets C++20 based GNU/Linux system monitor**
*Qt6 Widgets C++20 tabanlı GNU/Linux sistem monitörü*

SightProc is a lightweight, native C++20 system monitor and command center designed for GNU/Linux desktops (tested on Arch Linux / KDE Plasma Wayland). It renders purely through the active Qt6 `QStyle` and `QPalette` (zero QSS stylesheets and zero hardcoded UI colors), ensuring seamless visual integration with themes such as **Kvantum**, **Breeze**, and **Oxygen**.

---

## Key Features / Temel Özellikler

- **Pure Qt6 Widgets & Kvantum Compatible:** No `setStyleSheet`, no hardcoded hex colors, no heavy third-party chart libraries; live smooth-sliding `QPainter` area charts inherit active theme palette and translucent window surfaces naturally.
  *(Sıfır QSS ve sabit renk kodu; tüm canlı `QPainter` grafikleri ve yarı saydam tablolar sistem temasına ve Kvantum bulanıklaştırma efektine doğrudan uyum sağlar.)*
- **Performance Tab:** Live CPU (overall + per-core logical grid), Memory & Swap/ZRAM, Disks & NVMe throughput/IOPS/temp, Network interfaces, GPUs (DRM `sysfs` + runtime-loaded `libnvidia-ml.so` NVML), `hwmon` Fans, selectable multi-sensor `hwmon` Thermal plots, and system Battery telemetry (`/sys/class/power_supply`), with resizable vertical splitters and hover numeric callouts.
  *(Performans Sekmesi: CPU + mantıksal çekirdek ızgarası, Bellek/Swap, Diskler, Ağ, GPU, Fan devirleri, çoklu seçilebilir `hwmon` sıcaklık grafiği, sistem Bataryası ve kayan grafik / fare üzeri değer kutucuğu desteği.)*
- **Applications Tab:** Aggregates desktop GUI applications via `.desktop` matching and `cgroup` hierarchy (CPU, RSS, Shared, Disk I/O, Network, GPU %, VRAM).
  *(Uygulamalar Sekmesi: Masaüstü uygulamalarının alt süreç kaynak kullanımlarını tek satırda toplar.)*
- **Processes Tab:** Full `/proc` process table with expandable process tree or flat list mode, icons, binary name restoration beyond the 15-byte kernel `comm` limit, process inspection dialog, `nice` adjustment, and all POSIX signals (`SIGTERM`, `SIGKILL`, `SIGSTOP`, `SIGCONT`, etc.).
  *(Süreçler Sekmesi: Açılır süreç ağacı veya düz liste, ikon desteği, tüm POSIX sinyalleri ve süreç ayrıntı penceresi.)*
- **Services Tab:** Lists `systemd` units, controls start/stop/restart actions, and displays live `journalctl` logs.
  *(Servisler Sekmesi: `systemd` birimlerini yönetir ve seçili servisin `journalctl` günlüklerini gösterir.)*
- **Bilingual UI & Persistent Preferences:** Instant English / Turkish UI switching, configurable refresh interval (500 ms – 3 s), per-tab zoom (`Ctrl` + Wheel), and dynamic active Qt style + Wayland compositor / X11 WM detection in the status bar — stored cleanly in `~/.config/sightproc/sightproc.conf`.
  *(Anlık İngilizce / Türkçe dil desteği, kalıcı `~/.config/sightproc/sightproc.conf` yapılandırması ve durum çubuğunda aktif stil + pencere yöneticisi/besteci gösterimi.)*

---

## Development Transparency / Geliştirme Şeffaflığı

- **EN:** The architectural design, visual layout rules, hardware telemetry requirements, and user experience of SightProc were designed and directed by a human author; the C++20 and Qt6 Widgets source code was generated entirely using LLM agents.
- **TR:** Bu projede mimari tasarım, görsel ilkeler, telemetri kapsamı ve kullanıcı deneyimi tamamen insan tarafından kurgulanıp yönlendirilmiş; C++20 ve Qt6 Widgets kaynak kodu ise bütünüyle LLM ajanları ile üretilmiştir.

---

## Arch Linux Build & Run / Derleme ve Çalıştırma

### Dependencies / Bağımlılıklar

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg
```

### Build / Derleme

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Without Installing / Kurulumsuz Çalıştırma

The custom icon (`data/icons/sightproc.svg`) is embedded inside the binary via Qt resources, so you can launch SightProc directly from the build directory:

```bash
./build/sightproc
```

### System Installation (Optional) / Sistem Kurulumu (İsteğe Bağlı)

```bash
sudo cmake --install build --prefix /usr
```

This installs `/usr/bin/sightproc`, `/usr/share/icons/hicolor/scalable/apps/sightproc.svg`, and `/usr/share/applications/sightproc.desktop`.

---

## License / Lisans

All source code and the icon asset (`data/icons/sightproc.svg`) are released under the **MIT-0 (MIT No Attribution)** license. See [LICENSE](LICENSE) for the full text.

Tüm kaynak kodu ve `data/icons/sightproc.svg` ikonu **MIT-0 (MIT No Attribution)** lisansı altındadır. Tam metin için [LICENSE](LICENSE) dosyasına bakınız.
