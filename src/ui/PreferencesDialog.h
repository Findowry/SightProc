#pragma once

#include "core/AppSettings.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;

namespace sysmon {

class ColorSwatchButton;

/**
 * @brief Dil (İngilizce/Türkçe), şeffaflık, sekme ölçekleri, grafik renkleri ve
 *        süreç ağacı davranışını özelleştiren Tercihler penceresi.
 */
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

private slots:
    void retranslateUi();

private:
    void setupUi();
    void syncFromSettings();

    QTabWidget* m_tabs{nullptr};

    // Genel ve Dil
    QLabel* m_langLabel{nullptr};
    QComboBox* m_langCombo{nullptr};
    QLabel* m_refreshLabel{nullptr};
    QComboBox* m_refreshCombo{nullptr};
    QCheckBox* m_probeGpuCheck{nullptr};

    // Görünüm ve Şeffaflık
    QLabel* m_opacityLabel{nullptr};
    QSlider* m_opacitySlider{nullptr};
    QLabel* m_opacityValLabel{nullptr};
    QLabel* m_blurNoticeLabel{nullptr};
    QLabel* m_scaleHintLabel{nullptr};
    QLabel* m_tabScaleNames[4]{};
    QSlider* m_tabScaleSliders[4]{};
    QLabel* m_tabScaleVals[4]{};
    QPushButton* m_tabScaleResets[4]{};

    // Performans ve Renkler
    QLabel* m_presetLabel{nullptr};
    QComboBox* m_presetCombo{nullptr};
    QPushButton* m_applyPresetBtn{nullptr};
    QLabel* m_catLabels[static_cast<int>(AppSettings::HardwareCategory::Count)]{};
    ColorSwatchButton* m_catSwatches[static_cast<int>(AppSettings::HardwareCategory::Count)]{};
    QPushButton* m_catPickBtns[static_cast<int>(AppSettings::HardwareCategory::Count)]{};
    QLabel* m_lineWidthLabel{nullptr};
    QDoubleSpinBox* m_lineWidthSpin{nullptr};
    QLabel* m_fillOpacityLabel{nullptr};
    QSlider* m_fillOpacitySlider{nullptr};
    QLabel* m_fillOpacityValLabel{nullptr};
    QCheckBox* m_gridCheck{nullptr};
    QCheckBox* m_smoothScrollCheck{nullptr};
    QCheckBox* m_hoverValueCheck{nullptr};

    // Süreçler
    QCheckBox* m_treeModeCheck{nullptr};
    QCheckBox* m_showIconsCheck{nullptr};
    QCheckBox* m_confirmKillCheck{nullptr};

    QPushButton* m_closeBtn{nullptr};
    bool m_syncing{false};
};

} // namespace sysmon
