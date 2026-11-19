#pragma once

#include "core/Lang.h"

#include <QColor>
#include <QObject>
#include <QStringList>

namespace sysmon {

class AppSettings : public QObject {
    Q_OBJECT

public:
    enum class HardwareCategory {
        Cpu = 0,
        Memory,
        Disk,
        Network,
        Gpu,
        Fan,
        Thermal,
        Battery,
        Count
    };

    enum class ColorPreset {
        Vibrant = 0,
        Neon,
        Pastel,
        SystemPalette
    };

    static AppSettings& instance();

    [[nodiscard]] Language language() const noexcept { return m_language; }
    void setLanguage(Language lang);

    [[nodiscard]] int refreshIntervalMs() const noexcept { return m_refreshIntervalMs; }
    void setRefreshIntervalMs(int ms);

    [[nodiscard]] bool probeSleepingGpus() const noexcept { return m_probeSleepingGpus; }
    void setProbeSleepingGpus(bool enable);

    [[nodiscard]] int tableOpacityPercent() const noexcept { return m_tableOpacityPercent; }
    void setTableOpacityPercent(int percent);

    [[nodiscard]] int tabScalePercent(int tabIndex) const noexcept;
    void setTabScalePercent(int tabIndex, int percent);
    void resetTabScale(int tabIndex);
    static int defaultTabScalePercent(int tabIndex) noexcept;

    [[nodiscard]] ColorPreset colorPreset() const noexcept { return m_colorPreset; }
    void applyColorPreset(ColorPreset preset);

    [[nodiscard]] QColor categoryColor(HardwareCategory cat) const;
    void setCategoryColor(HardwareCategory cat, const QColor& color);

    [[nodiscard]] double chartLineWidth() const noexcept { return m_chartLineWidth; }
    void setChartLineWidth(double width);

    [[nodiscard]] int chartFillOpacityPercent() const noexcept { return m_chartFillOpacityPercent; }
    void setChartFillOpacityPercent(int percent);

    [[nodiscard]] bool chartShowGrid() const noexcept { return m_chartShowGrid; }
    void setChartShowGrid(bool show);

    [[nodiscard]] bool chartSmoothScroll() const noexcept { return m_chartSmoothScroll; }
    void setChartSmoothScroll(bool enabled);

    [[nodiscard]] bool chartShowHoverValue() const noexcept { return m_chartShowHoverValue; }
    void setChartShowHoverValue(bool show);

    [[nodiscard]] bool processTreeMode() const noexcept { return m_processTreeMode; }
    void setProcessTreeMode(bool enabled);

    [[nodiscard]] bool showProcessIcons() const noexcept { return m_showProcessIcons; }
    void setShowProcessIcons(bool enabled);

    [[nodiscard]] bool confirmSigkill() const noexcept { return m_confirmSigkill; }
    void setConfirmSigkill(bool enabled);

    [[nodiscard]] QStringList enabledIndicators(HardwareCategory cat) const;
    void setEnabledIndicators(HardwareCategory cat, const QStringList& ids);

    [[nodiscard]] QStringList enabledExtraSpecs(HardwareCategory cat) const;
    void setEnabledExtraSpecs(HardwareCategory cat, const QStringList& ids);

    [[nodiscard]] QStringList selectedThermalSensors() const noexcept { return m_selectedThermalSensors; }
    void setSelectedThermalSensors(const QStringList& ids);

    [[nodiscard]] static QString configDirPath();
    [[nodiscard]] static QString configFilePath();

    [[nodiscard]] static QString categoryKey(HardwareCategory cat);
    [[nodiscard]] static QString categoryDisplayName(HardwareCategory cat);

signals:
    void languageChanged();
    void refreshIntervalChanged(int ms);
    void probeSleepingGpusChanged(bool enabled);
    void tableOpacityChanged(int percent);
    void tabScaleChanged(int tabIndex, int percent);
    void chartAppearanceChanged();
    void performanceExtrasChanged();
    void selectedThermalSensorsChanged();
    void processPrefsChanged();

private:
    explicit AppSettings(QObject* parent = nullptr);
    [[nodiscard]] QColor defaultPresetColor(HardwareCategory cat, ColorPreset preset) const;
    void load();

    Language m_language{Language::English};
    int m_refreshIntervalMs{1000};
    bool m_probeSleepingGpus{false};
    int m_tableOpacityPercent{25};
    int m_tabScales[4]{115, 135, 100, 100};

    ColorPreset m_colorPreset{ColorPreset::Vibrant};
    QColor m_customColors[static_cast<int>(HardwareCategory::Count)];
    double m_chartLineWidth{2.0};
    int m_chartFillOpacityPercent{35};
    bool m_chartShowGrid{true};
    bool m_chartSmoothScroll{false};
    bool m_chartShowHoverValue{true};

    bool m_processTreeMode{true};
    bool m_showProcessIcons{true};
    bool m_confirmSigkill{true};

    QStringList m_indicators[static_cast<int>(HardwareCategory::Count)];
    QStringList m_extraSpecs[static_cast<int>(HardwareCategory::Count)];
    QStringList m_selectedThermalSensors;
};

} // namespace sysmon
