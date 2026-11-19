#include "core/AppSettings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QSettings>

#include <algorithm>

namespace sysmon {

namespace {

constexpr int kCatCount = static_cast<int>(AppSettings::HardwareCategory::Count);

float hueForCategory(AppSettings::HardwareCategory cat) {
    switch (cat) {
    case AppSettings::HardwareCategory::Cpu:
        return 0.56f;
    case AppSettings::HardwareCategory::Memory:
        return 0.76f;
    case AppSettings::HardwareCategory::Disk:
        return 0.39f;
    case AppSettings::HardwareCategory::Network:
        return 0.10f;
    case AppSettings::HardwareCategory::Gpu:
        return 0.02f;
    case AppSettings::HardwareCategory::Fan:
        return 0.48f;
    case AppSettings::HardwareCategory::Thermal:
        return 0.94f;
    case AppSettings::HardwareCategory::Battery:
        return 0.26f;
    default:
        return 0.56f;
    }
}

QStringList defaultIndicatorsFor(AppSettings::HardwareCategory cat) {
    if (cat == AppSettings::HardwareCategory::Gpu) {
        return {QStringLiteral("gpu_vram")};
    }
    if (cat == AppSettings::HardwareCategory::Fan) {
        return {QStringLiteral("fan_temp")};
    }
    if (cat == AppSettings::HardwareCategory::Battery) {
        return {QStringLiteral("bat_power")};
    }
    return {};
}

void saveSetting(const QString& key, const QVariant& value) {
    QDir().mkpath(AppSettings::configDirPath());
    QSettings s(AppSettings::configFilePath(), QSettings::IniFormat);
    s.setValue(key, value);
    s.sync();
}

void removeSetting(const QString& key) {
    QDir().mkpath(AppSettings::configDirPath());
    QSettings s(AppSettings::configFilePath(), QSettings::IniFormat);
    s.remove(key);
    s.sync();
}

} // namespace

QString AppSettings::configDirPath() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.config");
    }
    return QDir::cleanPath(base) + QStringLiteral("/sightproc");
}

QString AppSettings::configFilePath() {
    return configDirPath() + QStringLiteral("/sightproc.conf");
}

AppSettings& AppSettings::instance() {
    static AppSettings s_instance;
    return s_instance;
}

AppSettings::AppSettings(QObject* parent)
    : QObject(parent) {
    load();
}

void AppSettings::load() {
    QDir().mkpath(configDirPath());
    QSettings s(configFilePath(), QSettings::IniFormat);

    bool needsSync = false;
    auto readAppOrMigrateLegacy = [&](const QString& subKey, const QVariant& fallback) -> QVariant {
        const QString appKey = QStringLiteral("app/") + subKey;
        if (s.contains(appKey)) {
            return s.value(appKey, fallback);
        }
        const QString legacyKey = QStringLiteral("General/") + subKey;
        if (s.contains(legacyKey)) {
            const QVariant migrated = s.value(legacyKey, fallback);
            s.setValue(appKey, migrated);
            s.remove(legacyKey);
            needsSync = true;
            return migrated;
        }
        return fallback;
    };

    const int langInt = readAppOrMigrateLegacy(QStringLiteral("language"), 0).toInt();
    m_language = (langInt == 1) ? Language::Turkish : Language::English;
    setCurrentLanguage(m_language);

    m_refreshIntervalMs = std::clamp(
        readAppOrMigrateLegacy(QStringLiteral("refreshMs"), 1000).toInt(), 500, 5000);
    m_probeSleepingGpus =
        readAppOrMigrateLegacy(QStringLiteral("probeSleepingGpus"), false).toBool();

    m_tableOpacityPercent = std::clamp(
        s.value(QStringLiteral("appearance/tableOpacity"), 25).toInt(), 0, 100);

    for (int i = 0; i < 4; ++i) {
        const int def = defaultTabScalePercent(i);
        m_tabScales[i] = std::clamp(
            s.value(QStringLiteral("appearance/tabScale_%1").arg(i), def).toInt(), 70, 200);
    }

    const int presetInt = s.value(QStringLiteral("charts/colorPreset"), 0).toInt();
    m_colorPreset = static_cast<ColorPreset>(std::clamp(presetInt, 0, 3));
    m_chartLineWidth = std::clamp(
        s.value(QStringLiteral("charts/lineWidth"), 2.0).toDouble(), 1.5, 3.0);
    m_chartFillOpacityPercent = std::clamp(
        s.value(QStringLiteral("charts/fillOpacity"), 35).toInt(), 10, 60);
    m_chartShowGrid = s.value(QStringLiteral("charts/showGrid"), true).toBool();
    m_chartSmoothScroll = s.value(QStringLiteral("charts/smoothScroll"), false).toBool();
    m_chartShowHoverValue = s.value(QStringLiteral("charts/showHoverValue"), true).toBool();

    m_processTreeMode = s.value(QStringLiteral("processes/treeMode"), true).toBool();
    m_showProcessIcons = s.value(QStringLiteral("processes/showIcons"), true).toBool();
    m_confirmSigkill = s.value(QStringLiteral("processes/confirmSigkill"), true).toBool();

    for (int i = 0; i < kCatCount; ++i) {
        const auto cat = static_cast<HardwareCategory>(i);
        const QString key = categoryKey(cat);

        const QString savedColor = s.value(QStringLiteral("charts/color_%1").arg(key)).toString();
        if (!savedColor.isEmpty()) {
            const QColor parsed = QColor::fromString(savedColor);
            if (parsed.isValid()) {
                m_customColors[i] = parsed;
            }
        }

        const QString indKey = QStringLiteral("performance/indicators_%1").arg(key);
        if (s.contains(indKey)) {
            m_indicators[i] = s.value(indKey).toStringList();
        } else {
            m_indicators[i] = defaultIndicatorsFor(cat);
        }

        const QString specKey = QStringLiteral("performance/specs_%1").arg(key);
        if (s.contains(specKey)) {
            m_extraSpecs[i] = s.value(specKey).toStringList();
        }
    }

    m_selectedThermalSensors = s.value(QStringLiteral("performance/selectedThermals")).toStringList();

    if (!QFile::exists(configFilePath()) ||
        !s.contains(QStringLiteral("app/language")) ||
        !s.contains(QStringLiteral("app/refreshMs"))) {
        s.setValue(QStringLiteral("app/language"), static_cast<int>(m_language));
        s.setValue(QStringLiteral("app/refreshMs"), m_refreshIntervalMs);
        needsSync = true;
    }
    if (needsSync) {
        s.sync();
    }
}

void AppSettings::setLanguage(Language lang) {
    if (m_language == lang) {
        return;
    }
    m_language = lang;
    setCurrentLanguage(lang);
    saveSetting(QStringLiteral("app/language"), static_cast<int>(lang));
    emit languageChanged();
}

void AppSettings::setRefreshIntervalMs(int ms) {
    const int clamped = std::clamp(ms, 500, 5000);
    if (m_refreshIntervalMs == clamped) {
        return;
    }
    m_refreshIntervalMs = clamped;
    saveSetting(QStringLiteral("app/refreshMs"), clamped);
    emit refreshIntervalChanged(clamped);
}

void AppSettings::setProbeSleepingGpus(bool enable) {
    if (m_probeSleepingGpus == enable) {
        return;
    }
    m_probeSleepingGpus = enable;
    saveSetting(QStringLiteral("app/probeSleepingGpus"), enable);
    emit probeSleepingGpusChanged(enable);
}

void AppSettings::setTableOpacityPercent(int percent) {
    const int clamped = std::clamp(percent, 0, 100);
    if (m_tableOpacityPercent == clamped) {
        return;
    }
    m_tableOpacityPercent = clamped;
    saveSetting(QStringLiteral("appearance/tableOpacity"), clamped);
    emit tableOpacityChanged(clamped);
}

int AppSettings::defaultTabScalePercent(int tabIndex) noexcept {
    switch (tabIndex) {
    case 0:
        return 115;
    case 1:
        return 135;
    case 2:
    case 3:
    default:
        return 100;
    }
}

int AppSettings::tabScalePercent(int tabIndex) const noexcept {
    if (tabIndex < 0 || tabIndex >= 4) {
        return 100;
    }
    return m_tabScales[tabIndex];
}

void AppSettings::setTabScalePercent(int tabIndex, int percent) {
    if (tabIndex < 0 || tabIndex >= 4) {
        return;
    }
    const int clamped = std::clamp(percent, 70, 200);
    if (m_tabScales[tabIndex] == clamped) {
        return;
    }
    m_tabScales[tabIndex] = clamped;
    saveSetting(QStringLiteral("appearance/tabScale_%1").arg(tabIndex), clamped);
    emit tabScaleChanged(tabIndex, clamped);
}

void AppSettings::resetTabScale(int tabIndex) {
    setTabScalePercent(tabIndex, defaultTabScalePercent(tabIndex));
}

QColor AppSettings::defaultPresetColor(HardwareCategory cat, ColorPreset preset) const {
    if (preset == ColorPreset::SystemPalette) {
        return QApplication::palette().color(QPalette::Highlight);
    }

    const QColor win = QApplication::palette().color(QPalette::Window);
    const bool isDarkTheme = (win.lightnessF() < 0.5f);
    const float hue = hueForCategory(cat);

    switch (preset) {
    case ColorPreset::Vibrant:
        return QColor::fromHslF(hue, isDarkTheme ? 0.85f : 0.78f, isDarkTheme ? 0.62f : 0.44f);
    case ColorPreset::Neon:
        return QColor::fromHslF(hue, 0.96f, isDarkTheme ? 0.66f : 0.48f);
    case ColorPreset::Pastel:
        return QColor::fromHslF(hue, 0.62f, isDarkTheme ? 0.72f : 0.52f);
    case ColorPreset::SystemPalette:
    default:
        return QApplication::palette().color(QPalette::Highlight);
    }
}

void AppSettings::applyColorPreset(ColorPreset preset) {
    m_colorPreset = preset;
    saveSetting(QStringLiteral("charts/colorPreset"), static_cast<int>(preset));
    for (int i = 0; i < kCatCount; ++i) {
        QColor emptyCol;
        m_customColors[i] = emptyCol;
        removeSetting(QStringLiteral("charts/color_%1").arg(categoryKey(static_cast<HardwareCategory>(i))));
    }
    emit chartAppearanceChanged();
}

QColor AppSettings::categoryColor(HardwareCategory cat) const {
    const int idx = static_cast<int>(cat);
    if (idx >= 0 && idx < kCatCount && m_customColors[idx].isValid()) {
        return m_customColors[idx];
    }
    return defaultPresetColor(cat, m_colorPreset);
}

void AppSettings::setCategoryColor(HardwareCategory cat, const QColor& color) {
    const int idx = static_cast<int>(cat);
    if (idx < 0 || idx >= kCatCount || !color.isValid()) {
        return;
    }
    m_customColors[idx] = color;
    saveSetting(QStringLiteral("charts/color_%1").arg(categoryKey(cat)), color.name(QColor::HexRgb));
    emit chartAppearanceChanged();
}

void AppSettings::setChartLineWidth(double width) {
    const double clamped = std::clamp(width, 1.5, 3.0);
    if (qFuzzyCompare(m_chartLineWidth, clamped)) {
        return;
    }
    m_chartLineWidth = clamped;
    saveSetting(QStringLiteral("charts/lineWidth"), clamped);
    emit chartAppearanceChanged();
}

void AppSettings::setChartFillOpacityPercent(int percent) {
    const int clamped = std::clamp(percent, 10, 60);
    if (m_chartFillOpacityPercent == clamped) {
        return;
    }
    m_chartFillOpacityPercent = clamped;
    saveSetting(QStringLiteral("charts/fillOpacity"), clamped);
    emit chartAppearanceChanged();
}

void AppSettings::setChartShowGrid(bool show) {
    if (m_chartShowGrid == show) {
        return;
    }
    m_chartShowGrid = show;
    saveSetting(QStringLiteral("charts/showGrid"), show);
    emit chartAppearanceChanged();
}

void AppSettings::setChartSmoothScroll(bool enabled) {
    if (m_chartSmoothScroll == enabled) {
        return;
    }
    m_chartSmoothScroll = enabled;
    saveSetting(QStringLiteral("charts/smoothScroll"), enabled);
    emit chartAppearanceChanged();
}

void AppSettings::setChartShowHoverValue(bool show) {
    if (m_chartShowHoverValue == show) {
        return;
    }
    m_chartShowHoverValue = show;
    saveSetting(QStringLiteral("charts/showHoverValue"), show);
    emit chartAppearanceChanged();
}

void AppSettings::setProcessTreeMode(bool enabled) {
    if (m_processTreeMode == enabled) {
        return;
    }
    m_processTreeMode = enabled;
    saveSetting(QStringLiteral("processes/treeMode"), enabled);
    emit processPrefsChanged();
}

void AppSettings::setShowProcessIcons(bool enabled) {
    if (m_showProcessIcons == enabled) {
        return;
    }
    m_showProcessIcons = enabled;
    saveSetting(QStringLiteral("processes/showIcons"), enabled);
    emit processPrefsChanged();
}

void AppSettings::setConfirmSigkill(bool enabled) {
    if (m_confirmSigkill == enabled) {
        return;
    }
    m_confirmSigkill = enabled;
    saveSetting(QStringLiteral("processes/confirmSigkill"), enabled);
    emit processPrefsChanged();
}

QStringList AppSettings::enabledIndicators(HardwareCategory cat) const {
    const int idx = static_cast<int>(cat);
    if (idx < 0 || idx >= kCatCount) {
        return {};
    }
    return m_indicators[idx];
}

void AppSettings::setEnabledIndicators(HardwareCategory cat, const QStringList& ids) {
    const int idx = static_cast<int>(cat);
    if (idx < 0 || idx >= kCatCount || m_indicators[idx] == ids) {
        return;
    }
    m_indicators[idx] = ids;
    saveSetting(QStringLiteral("performance/indicators_%1").arg(categoryKey(cat)), ids);
    emit performanceExtrasChanged();
}

QStringList AppSettings::enabledExtraSpecs(HardwareCategory cat) const {
    const int idx = static_cast<int>(cat);
    if (idx < 0 || idx >= kCatCount) {
        return {};
    }
    return m_extraSpecs[idx];
}

void AppSettings::setEnabledExtraSpecs(HardwareCategory cat, const QStringList& ids) {
    const int idx = static_cast<int>(cat);
    if (idx < 0 || idx >= kCatCount || m_extraSpecs[idx] == ids) {
        return;
    }
    m_extraSpecs[idx] = ids;
    saveSetting(QStringLiteral("performance/specs_%1").arg(categoryKey(cat)), ids);
    emit performanceExtrasChanged();
}

void AppSettings::setSelectedThermalSensors(const QStringList& ids) {
    if (m_selectedThermalSensors == ids) {
        return;
    }
    m_selectedThermalSensors = ids;
    saveSetting(QStringLiteral("performance/selectedThermals"), ids);
    emit selectedThermalSensorsChanged();
}

QString AppSettings::categoryKey(HardwareCategory cat) {
    switch (cat) {
    case HardwareCategory::Cpu:
        return QStringLiteral("cpu");
    case HardwareCategory::Memory:
        return QStringLiteral("memory");
    case HardwareCategory::Disk:
        return QStringLiteral("disk");
    case HardwareCategory::Network:
        return QStringLiteral("network");
    case HardwareCategory::Gpu:
        return QStringLiteral("gpu");
    case HardwareCategory::Fan:
        return QStringLiteral("fan");
    case HardwareCategory::Thermal:
        return QStringLiteral("thermal");
    case HardwareCategory::Battery:
        return QStringLiteral("battery");
    default:
        return QStringLiteral("other");
    }
}

QString AppSettings::categoryDisplayName(HardwareCategory cat) {
    switch (cat) {
    case HardwareCategory::Cpu:
        return trText("CPU", "İşlemci (CPU)");
    case HardwareCategory::Memory:
        return trText("Memory", "Bellek");
    case HardwareCategory::Disk:
        return trText("Disks", "Diskler");
    case HardwareCategory::Network:
        return trText("Network", "Ağ");
    case HardwareCategory::Gpu:
        return trText("GPU", "Ekran Kartı (GPU)");
    case HardwareCategory::Fan:
        return trText("Fans", "Fanlar");
    case HardwareCategory::Thermal:
        return trText("Thermals", "Termal Sensörler");
    case HardwareCategory::Battery:
        return trText("Battery", "Batarya");
    default:
        return QStringLiteral("—");
    }
}

} // namespace sysmon
