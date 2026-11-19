#include "ui/PreferencesDialog.h"

#include "core/Lang.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sysmon {

class ColorSwatchButton : public QWidget {
public:
    explicit ColorSwatchButton(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAutoFillBackground(false);
        setFixedSize(40, 20);
    }

    void setSwatchColor(const QColor& color) {
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor border = QApplication::palette().color(QPalette::Mid);
        border.setAlphaF(0.65f);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(m_color.isValid()
                             ? m_color
                             : QApplication::palette().color(QPalette::Highlight));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 4.0, 4.0);
    }

private:
    QColor m_color;
};

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
    syncFromSettings();
    retranslateUi();

    connect(&AppSettings::instance(), &AppSettings::languageChanged,
            this, &PreferencesDialog::retranslateUi);
    connect(&AppSettings::instance(), &AppSettings::chartAppearanceChanged,
            this, &PreferencesDialog::syncFromSettings);
    connect(&AppSettings::instance(), &AppSettings::tabScaleChanged,
            this, &PreferencesDialog::syncFromSettings);
    connect(&AppSettings::instance(), &AppSettings::tableOpacityChanged,
            this, &PreferencesDialog::syncFromSettings);
}

void PreferencesDialog::setupUi() {
    resize(680, 580);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_tabs = new QTabWidget(this);

    // 1. Genel ve Dil
    auto* genPage = new QWidget(m_tabs);
    auto* genForm = new QFormLayout(genPage);
    genForm->setContentsMargins(14, 14, 14, 14);
    genForm->setSpacing(12);

    m_langLabel = new QLabel(genPage);
    m_langCombo = new QComboBox(genPage);
    m_langCombo->addItem(QStringLiteral("English"), static_cast<int>(Language::English));
    m_langCombo->addItem(QStringLiteral("Türkçe"), static_cast<int>(Language::Turkish));

    m_refreshLabel = new QLabel(genPage);
    m_refreshCombo = new QComboBox(genPage);
    m_refreshCombo->addItem(QStringLiteral("500 ms"), 500);
    m_refreshCombo->addItem(QStringLiteral("1000 ms (1 s)"), 1000);
    m_refreshCombo->addItem(QStringLiteral("2000 ms (2 s)"), 2000);
    m_refreshCombo->addItem(QStringLiteral("3000 ms (3 s)"), 3000);

    m_probeGpuCheck = new QCheckBox(genPage);

    genForm->addRow(m_langLabel, m_langCombo);
    genForm->addRow(m_refreshLabel, m_refreshCombo);
    genForm->addRow(QString(), m_probeGpuCheck);
    m_tabs->addTab(genPage, QString());

    // 2. Görünüm ve Şeffaflık
    auto* appPage = new QWidget(m_tabs);
    auto* appLayout = new QVBoxLayout(appPage);
    appLayout->setContentsMargins(14, 14, 14, 14);
    appLayout->setSpacing(12);

    auto* opRow = new QHBoxLayout();
    m_opacityLabel = new QLabel(appPage);
    m_opacitySlider = new QSlider(Qt::Horizontal, appPage);
    m_opacitySlider->setRange(0, 100);
    m_opacityValLabel = new QLabel(appPage);
    m_opacityValLabel->setMinimumWidth(46);
    opRow->addWidget(m_opacityLabel);
    opRow->addWidget(m_opacitySlider, 1);
    opRow->addWidget(m_opacityValLabel);
    appLayout->addLayout(opRow);

    m_blurNoticeLabel = new QLabel(appPage);
    m_blurNoticeLabel->setWordWrap(true);
    {
        QFont nf = m_blurNoticeLabel->font();
        nf.setItalic(true);
        m_blurNoticeLabel->setFont(nf);
    }
    appLayout->addWidget(m_blurNoticeLabel);

    m_scaleHintLabel = new QLabel(appPage);
    m_scaleHintLabel->setWordWrap(true);
    appLayout->addWidget(m_scaleHintLabel);

    auto* scaleGrid = new QGridLayout();
    scaleGrid->setHorizontalSpacing(10);
    scaleGrid->setVerticalSpacing(10);
    for (int i = 0; i < 4; ++i) {
        m_tabScaleNames[i] = new QLabel(appPage);
        m_tabScaleSliders[i] = new QSlider(Qt::Horizontal, appPage);
        m_tabScaleSliders[i]->setRange(70, 200);
        m_tabScaleVals[i] = new QLabel(appPage);
        m_tabScaleVals[i]->setMinimumWidth(46);
        m_tabScaleResets[i] = new QPushButton(appPage);

        scaleGrid->addWidget(m_tabScaleNames[i], i, 0);
        scaleGrid->addWidget(m_tabScaleSliders[i], i, 1);
        scaleGrid->addWidget(m_tabScaleVals[i], i, 2);
        scaleGrid->addWidget(m_tabScaleResets[i], i, 3);

        connect(m_tabScaleSliders[i], &QSlider::valueChanged, this, [this, i](int val) {
            if (!m_syncing) {
                AppSettings::instance().setTabScalePercent(i, val);
            }
        });
        connect(m_tabScaleResets[i], &QPushButton::clicked, this, [i]() {
            AppSettings::instance().resetTabScale(i);
        });
    }
    appLayout->addLayout(scaleGrid);
    appLayout->addStretch(1);
    m_tabs->addTab(appPage, QString());

    // 3. Performans ve Renkler
    auto* chartPage = new QWidget(m_tabs);
    auto* chartLayout = new QVBoxLayout(chartPage);
    chartLayout->setContentsMargins(14, 14, 14, 14);
    chartLayout->setSpacing(10);

    auto* presetRow = new QHBoxLayout();
    m_presetLabel = new QLabel(chartPage);
    m_presetCombo = new QComboBox(chartPage);
    m_presetCombo->addItem(QString(), static_cast<int>(AppSettings::ColorPreset::Vibrant));
    m_presetCombo->addItem(QString(), static_cast<int>(AppSettings::ColorPreset::Neon));
    m_presetCombo->addItem(QString(), static_cast<int>(AppSettings::ColorPreset::Pastel));
    m_presetCombo->addItem(QString(), static_cast<int>(AppSettings::ColorPreset::SystemPalette));
    m_applyPresetBtn = new QPushButton(chartPage);
    presetRow->addWidget(m_presetLabel);
    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(m_applyPresetBtn);
    chartLayout->addLayout(presetRow);

    auto* colorGrid = new QGridLayout();
    colorGrid->setHorizontalSpacing(10);
    colorGrid->setVerticalSpacing(6);
    constexpr int kCats = static_cast<int>(AppSettings::HardwareCategory::Count);
    for (int i = 0; i < kCats; ++i) {
        const auto cat = static_cast<AppSettings::HardwareCategory>(i);
        m_catLabels[i] = new QLabel(chartPage);
        m_catSwatches[i] = new ColorSwatchButton(chartPage);
        m_catPickBtns[i] = new QPushButton(chartPage);
        colorGrid->addWidget(m_catLabels[i], i, 0);
        colorGrid->addWidget(m_catSwatches[i], i, 1);
        colorGrid->addWidget(m_catPickBtns[i], i, 2);

        connect(m_catPickBtns[i], &QPushButton::clicked, this, [this, cat]() {
            const QColor cur = AppSettings::instance().categoryColor(cat);
            const QColor chosen = QColorDialog::getColor(
                cur,
                this,
                trText("Select Category Color", "Kategori Rengi Seç"));
            if (chosen.isValid()) {
                AppSettings::instance().setCategoryColor(cat, chosen);
            }
        });
    }
    chartLayout->addLayout(colorGrid);

    auto* styleForm = new QFormLayout();
    m_lineWidthLabel = new QLabel(chartPage);
    m_lineWidthSpin = new QDoubleSpinBox(chartPage);
    m_lineWidthSpin->setRange(1.5, 3.0);
    m_lineWidthSpin->setSingleStep(0.25);
    m_lineWidthSpin->setSuffix(QStringLiteral(" px"));

    auto* fillRow = new QHBoxLayout();
    m_fillOpacityLabel = new QLabel(chartPage);
    m_fillOpacitySlider = new QSlider(Qt::Horizontal, chartPage);
    m_fillOpacitySlider->setRange(10, 60);
    m_fillOpacityValLabel = new QLabel(chartPage);
    m_fillOpacityValLabel->setMinimumWidth(46);
    fillRow->addWidget(m_fillOpacitySlider, 1);
    fillRow->addWidget(m_fillOpacityValLabel);

    m_gridCheck = new QCheckBox(chartPage);
    m_smoothScrollCheck = new QCheckBox(chartPage);
    m_hoverValueCheck = new QCheckBox(chartPage);

    styleForm->addRow(m_lineWidthLabel, m_lineWidthSpin);
    styleForm->addRow(m_fillOpacityLabel, fillRow);
    styleForm->addRow(QString(), m_gridCheck);
    styleForm->addRow(QString(), m_smoothScrollCheck);
    styleForm->addRow(QString(), m_hoverValueCheck);
    chartLayout->addLayout(styleForm);
    chartLayout->addStretch(1);
    m_tabs->addTab(chartPage, QString());

    // 4. Süreçler
    auto* procPage = new QWidget(m_tabs);
    auto* procLayout = new QVBoxLayout(procPage);
    procLayout->setContentsMargins(14, 14, 14, 14);
    procLayout->setSpacing(10);

    m_treeModeCheck = new QCheckBox(procPage);
    m_showIconsCheck = new QCheckBox(procPage);
    m_confirmKillCheck = new QCheckBox(procPage);

    procLayout->addWidget(m_treeModeCheck);
    procLayout->addWidget(m_showIconsCheck);
    procLayout->addWidget(m_confirmKillCheck);
    procLayout->addStretch(1);
    m_tabs->addTab(procPage, QString());

    root->addWidget(m_tabs, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_closeBtn = new QPushButton(this);
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);

    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_langCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (m_syncing) {
            return;
        }
        const auto lang = static_cast<Language>(m_langCombo->itemData(idx).toInt());
        AppSettings::instance().setLanguage(lang);
    });
    connect(m_refreshCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_syncing) {
            AppSettings::instance().setRefreshIntervalMs(m_refreshCombo->itemData(idx).toInt());
        }
    });
    connect(m_probeGpuCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setProbeSleepingGpus(on);
        }
    });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int val) {
        if (!m_syncing) {
            AppSettings::instance().setTableOpacityPercent(val);
        }
    });
    connect(m_applyPresetBtn, &QPushButton::clicked, this, [this]() {
        const auto preset = static_cast<AppSettings::ColorPreset>(
            m_presetCombo->currentData().toInt());
        AppSettings::instance().applyColorPreset(preset);
    });
    connect(m_lineWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double w) {
                if (!m_syncing) {
                    AppSettings::instance().setChartLineWidth(w);
                }
            });
    connect(m_fillOpacitySlider, &QSlider::valueChanged, this, [this](int val) {
        if (!m_syncing) {
            AppSettings::instance().setChartFillOpacityPercent(val);
        }
    });
    connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setChartShowGrid(on);
        }
    });
    connect(m_smoothScrollCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setChartSmoothScroll(on);
        }
    });
    connect(m_hoverValueCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setChartShowHoverValue(on);
        }
    });
    connect(m_treeModeCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setProcessTreeMode(on);
        }
    });
    connect(m_showIconsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setShowProcessIcons(on);
        }
    });
    connect(m_confirmKillCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing) {
            AppSettings::instance().setConfirmSigkill(on);
        }
    });
}

void PreferencesDialog::syncFromSettings() {
    m_syncing = true;
    const auto& s = AppSettings::instance();

    const int langIdx = m_langCombo->findData(static_cast<int>(s.language()));
    if (langIdx >= 0) {
        m_langCombo->setCurrentIndex(langIdx);
    }

    const int refIdx = m_refreshCombo->findData(s.refreshIntervalMs());
    if (refIdx >= 0) {
        m_refreshCombo->setCurrentIndex(refIdx);
    }
    m_probeGpuCheck->setChecked(s.probeSleepingGpus());

    m_opacitySlider->setValue(s.tableOpacityPercent());
    m_opacityValLabel->setText(QStringLiteral("%1%").arg(s.tableOpacityPercent()));

    for (int i = 0; i < 4; ++i) {
        const int sc = s.tabScalePercent(i);
        m_tabScaleSliders[i]->setValue(sc);
        m_tabScaleVals[i]->setText(QStringLiteral("%1%").arg(sc));
    }

    const int pIdx = m_presetCombo->findData(static_cast<int>(s.colorPreset()));
    if (pIdx >= 0) {
        m_presetCombo->setCurrentIndex(pIdx);
    }
    constexpr int kCats = static_cast<int>(AppSettings::HardwareCategory::Count);
    for (int i = 0; i < kCats; ++i) {
        const auto cat = static_cast<AppSettings::HardwareCategory>(i);
        m_catSwatches[i]->setSwatchColor(s.categoryColor(cat));
    }
    m_lineWidthSpin->setValue(s.chartLineWidth());
    m_fillOpacitySlider->setValue(s.chartFillOpacityPercent());
    m_fillOpacityValLabel->setText(QStringLiteral("%1%").arg(s.chartFillOpacityPercent()));
    m_gridCheck->setChecked(s.chartShowGrid());
    m_smoothScrollCheck->setChecked(s.chartSmoothScroll());
    m_hoverValueCheck->setChecked(s.chartShowHoverValue());

    m_treeModeCheck->setChecked(s.processTreeMode());
    m_showIconsCheck->setChecked(s.showProcessIcons());
    m_confirmKillCheck->setChecked(s.confirmSigkill());

    m_syncing = false;
}

void PreferencesDialog::retranslateUi() {
    setWindowTitle(trText("Preferences", "Tercihler"));

    m_tabs->setTabText(0, trText("General & Language", "Genel ve Dil"));
    m_tabs->setTabText(1, trText("Appearance & Transparency", "Görünüm ve Şeffaflık"));
    m_tabs->setTabText(2, trText("Performance & Colors", "Performans ve Renkler"));
    m_tabs->setTabText(3, trText("Processes", "Süreçler"));

    m_langLabel->setText(trText("Language:", "Dil:"));
    m_refreshLabel->setText(trText("Refresh Interval:", "Yenileme Aralığı:"));
    m_probeGpuCheck->setText(trText("Probe sleeping NVIDIA GPU (D3cold)",
                                    "Uyuyan NVIDIA GPU'yu yokla (D3cold)"));

    m_opacityLabel->setText(trText("Table & Panel Opacity (Blur Transparency):",
                                   "Tablo ve Panel Opaklığı (Blur Şeffaflığı):"));
    m_blurNoticeLabel->setText(
        trText("The theme you are using must support background blur.",
               "Kullandığınız temanın bulanıklaştırma özelliğine sahip olması gerekmektedir."));
    m_scaleHintLabel->setText(
        trText("Each tab remembers its own independent size scale. Tip: Hold Ctrl + Mouse Wheel inside any tab to resize it live.",
               "Her sekme kendi bağımsız boyut ölçeğini saklar. İpucu: Herhangi bir sekmede Ctrl + Fare Tekerleği ile anlık boyutlandırabilirsiniz."));

    const QString tabNames[4] = {
        trText("Performance Tab Scale:", "Performans Sekmesi Ölçeği:"),
        trText("Applications Tab Scale:", "Uygulamalar Sekmesi Ölçeği:"),
        trText("Processes Tab Scale:", "Süreçler Sekmesi Ölçeği:"),
        trText("Services Tab Scale:", "Servisler Sekmesi Ölçeği:"),
    };
    for (int i = 0; i < 4; ++i) {
        m_tabScaleNames[i]->setText(tabNames[i]);
        m_tabScaleResets[i]->setText(trText("Reset", "Sıfırla"));
    }

    m_presetLabel->setText(trText("Color Preset:", "Renk Teması:"));
    m_presetCombo->setItemText(0, trText("Vibrant Hardware", "Canlı Donanım"));
    m_presetCombo->setItemText(1, trText("Neon Command", "Neon Komuta"));
    m_presetCombo->setItemText(2, trText("Soft Pastel", "Yumuşak Pastel"));
    m_presetCombo->setItemText(3, trText("System Theme Accent", "Sistem Tema Vurgusu"));
    m_applyPresetBtn->setText(trText("Apply Preset", "Şablonu Uygula"));

    constexpr int kCats = static_cast<int>(AppSettings::HardwareCategory::Count);
    for (int i = 0; i < kCats; ++i) {
        const auto cat = static_cast<AppSettings::HardwareCategory>(i);
        m_catLabels[i]->setText(AppSettings::categoryDisplayName(cat) + QLatin1Char(':'));
        m_catPickBtns[i]->setText(trText("Pick Color...", "Renk Seç..."));
    }

    m_lineWidthLabel->setText(trText("Chart Line Width:", "Grafik Çizgi Kalınlığı:"));
    m_fillOpacityLabel->setText(trText("Area Fill Opacity:", "Alan Dolgu Yoğunluğu:"));
    m_gridCheck->setText(trText("Show background grid lines", "Arka plan ızgara çizgilerini göster"));
    m_smoothScrollCheck->setText(trText("Smooth sliding charts (animated updates)",
                                        "Kayan grafikler (pürüzsüz yenileme)"));
    m_hoverValueCheck->setText(trText("Show numeric values when hovering charts",
                                      "Grafiklerin üzerine gelince sayısal değerleri göster"));

    m_treeModeCheck->setText(trText("Show processes as expandable tree (forked hierarchy)",
                                    "Süreçleri açılır ağaç (çatallanan hiyerarşi) olarak göster"));
    m_showIconsCheck->setText(trText("Show application & binary icons in Processes tab",
                                     "Süreçler sekmesinde uygulama ve ikili dosya ikonlarını göster"));
    m_confirmKillCheck->setText(trText("Confirm before sending destructive signals (SIGKILL, etc.)",
                                       "Yıkıcı sinyaller (SIGKILL vb.) öncesinde onay iste"));

    m_closeBtn->setText(trText("Close", "Kapat"));
}

} // namespace sysmon
