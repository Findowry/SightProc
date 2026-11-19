#include "widgets/CoreGridWidget.h"

#include "core/AppSettings.h"
#include "core/FormatUtils.h"
#include "widgets/AreaChartWidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace sysmon {

CoreGridWidget::CoreGridWidget(QWidget* parent)
    : QScrollArea(parent) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    m_container = new QWidget(this);
    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(2, 2, 2, 2);
    m_grid->setSpacing(8);
    setWidget(m_container);
}

void CoreGridWidget::rebuildGrid(int coreCount) {
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    m_tiles.clear();

    if (coreCount <= 0) {
        return;
    }

    int cols = 4;
    if (coreCount <= 4) {
        cols = 2;
    } else if (coreCount > 16) {
        cols = 5;
    }

    m_tiles.reserve(coreCount);
    for (int i = 0; i < coreCount; ++i) {
        auto* card = new QFrame(m_container);
        card->setFrameShape(QFrame::StyledPanel);
        card->setFrameShadow(QFrame::Plain);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 6, 8, 6);
        cardLayout->setSpacing(4);

        auto* headerRow = new QHBoxLayout();
        auto* nameLabel = new QLabel(QStringLiteral("CPU %1").arg(i), card);
        QFont f = nameLabel->font();
        f.setBold(true);
        nameLabel->setFont(f);

        auto* statLabel = new QLabel(QStringLiteral("0.0%"), card);
        statLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        headerRow->addWidget(nameLabel);
        headerRow->addWidget(statLabel, 1);
        cardLayout->addLayout(headerRow);

        auto* chart = new AreaChartWidget(card);
        chart->setDisplayMode(AreaChartWidget::DisplayMode::Sparkline);
        chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
        chart->setFixedRange(0.0, 100.0);
        chart->setMinimumHeight(58);
        cardLayout->addWidget(chart, 1);

        m_grid->addWidget(card, i / cols, i % cols);
        m_tiles.push_back(CoreTile{nameLabel, statLabel, chart});
    }
    applyVisualSettings();
}

void CoreGridWidget::applyVisualSettings() {
    const auto& s = AppSettings::instance();
    const QColor cpuColor = s.categoryColor(AppSettings::HardwareCategory::Cpu);
    const int scale = s.tabScalePercent(0);
    const int minH = std::max(44, (58 * scale) / 100);
    for (auto& tile : m_tiles) {
        if (tile.chart) {
            tile.chart->setCustomPrimaryColor(cpuColor);
            tile.chart->setLineWidth(s.chartLineWidth());
            tile.chart->setFillOpacityPercent(s.chartFillOpacityPercent());
            tile.chart->setSmoothScrollEnabled(s.chartSmoothScroll());
            tile.chart->setScrollDurationMs(s.refreshIntervalMs());
            tile.chart->setMinimumHeight(minH);
        }
    }
}

void CoreGridWidget::updateCores(const QVector<CoreTelemetry>& cores) {
    if (cores.size() != m_tiles.size()) {
        rebuildGrid(static_cast<int>(cores.size()));
    }

    for (int i = 0; i < cores.size() && i < m_tiles.size(); ++i) {
        const CoreTelemetry& core = cores[i];
        CoreTile& tile = m_tiles[i];
        tile.nameLabel->setText(QStringLiteral("CPU %1").arg(core.id));
        if (core.curFreqMHz > 0.0) {
            tile.statLabel->setText(QStringLiteral("%1 · %2")
                                        .arg(fmt::formatPercent(core.usagePercent),
                                             fmt::formatFrequencyMHz(core.curFreqMHz)));
        } else {
            tile.statLabel->setText(fmt::formatPercent(core.usagePercent));
        }
        tile.chart->pushSample(core.usagePercent);
    }
}

} // namespace sysmon
