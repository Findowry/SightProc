#include "widgets/CombinedCpuWidget.h"

#include "core/AppSettings.h"
#include "core/FormatUtils.h"
#include "widgets/AreaChartWidget.h"

#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sysmon {

CpuLegendItem::CpuLegendItem(int coreIndex, QWidget* parent)
    : QWidget(parent)
    , m_coreIndex(coreIndex)
    , m_coreId(coreIndex) {
    setAutoFillBackground(false);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void CpuLegendItem::setCoreData(int coreId,
                                const QColor& color,
                                double usagePercent,
                                double curFreqMHz) {
    const bool idChanged = (m_coreId != coreId);
    m_coreId = coreId;
    m_color = color;
    m_usagePercent = usagePercent;
    m_freqMHz = curFreqMHz;

    if (m_freqMHz > 0.0) {
        setToolTip(QStringLiteral("CPU %1 — %2 · %3")
                       .arg(m_coreId)
                       .arg(fmt::formatPercent(m_usagePercent),
                            fmt::formatFrequencyMHz(m_freqMHz)));
    } else {
        setToolTip(QStringLiteral("CPU %1 — %2")
                       .arg(m_coreId)
                       .arg(fmt::formatPercent(m_usagePercent)));
    }

    if (idChanged) {
        updateGeometry();
    }
    update();
}

void CpuLegendItem::setSeriesColor(const QColor& color) {
    if (m_color == color) {
        return;
    }
    m_color = color;
    update();
}

void CpuLegendItem::setHoverStates(bool isHovered, bool isDimmed) {
    if (m_hovered == isHovered && m_dimmed == isDimmed) {
        return;
    }
    m_hovered = isHovered;
    m_dimmed = isDimmed;
    update();
}

QSize CpuLegendItem::sizeHint() const {
    const QFontMetrics fm(font());
    const int maxId = std::max(99, m_coreId);
    const QString sampleLabel = QStringLiteral("CPU %1").arg(maxId);
    const QString samplePct = QStringLiteral("100.0%");
    const int w = 6 + 13 + 6 + fm.horizontalAdvance(sampleLabel) + 8 +
                  fm.horizontalAdvance(samplePct) + 6;
    const int h = std::max(20, fm.height() + 6);
    return QSize(w, h);
}

QSize CpuLegendItem::minimumSizeHint() const {
    return sizeHint();
}

void CpuLegendItem::enterEvent(QEnterEvent* event) {
    emit hovered(m_coreIndex);
    QWidget::enterEvent(event);
}

void CpuLegendItem::leaveEvent(QEvent* event) {
    emit unhovered(m_coreIndex);
    QWidget::leaveEvent(event);
}

void CpuLegendItem::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_dimmed) {
        painter.setOpacity(0.45);
    }

    const QPalette pal = QApplication::palette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_hovered) {
        QColor bg = pal.color(QPalette::Highlight);
        bg.setAlphaF(0.18f);
        QColor border = pal.color(QPalette::Highlight);
        border.setAlphaF(0.55f);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(bg);
        painter.drawRoundedRect(r, 4.0, 4.0);
    }

    const double midY = r.center().y();
    const QRectF swatchRect(r.left() + 6.0, midY - 2.5, 13.0, 5.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color.isValid() ? m_color : pal.color(QPalette::Highlight));
    painter.drawRoundedRect(swatchRect, 2.5, 2.5);

    const QFontMetrics fmVal(font());
    const int pctWidth = fmVal.horizontalAdvance(QStringLiteral("100.0%")) + 4;
    const double textLeft = swatchRect.right() + 6.0;
    const double labelWidth = std::max(10.0, r.right() - 6.0 - pctWidth - textLeft);

    QFont labelFont = font();
    if (m_hovered) {
        labelFont.setBold(true);
    }
    painter.setFont(labelFont);
    painter.setPen(pal.color(QPalette::Text));
    painter.drawText(QRectF(textLeft, r.top(), labelWidth, r.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("CPU %1").arg(m_coreId));

    painter.setFont(font());
    QColor pctColor = pal.color(QPalette::Text);
    if (!m_hovered) {
        pctColor.setAlphaF(0.82f);
    }
    painter.setPen(pctColor);
    painter.drawText(QRectF(r.right() - 6.0 - pctWidth, r.top(),
                            static_cast<double>(pctWidth), r.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     fmt::formatPercent(m_usagePercent));
}

CombinedCpuWidget::CombinedCpuWidget(QWidget* parent)
    : QWidget(parent) {
    setAutoFillBackground(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_chart = new AreaChartWidget(this);
    m_chart->setDisplayMode(AreaChartWidget::DisplayMode::Detailed);
    m_chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
    m_chart->setFixedRange(0.0, 100.0);
    m_chart->setUnitSuffix(QStringLiteral("%"));
    m_chart->setInternalLegendVisible(false);
    m_chart->setMultiSeriesFillEnabled(false);
    m_chart->setMinimumHeight(160);
    layout->addWidget(m_chart, 1);

    m_legendScroll = new QScrollArea(this);
    m_legendScroll->setWidgetResizable(true);
    m_legendScroll->setFrameShape(QFrame::NoFrame);
    m_legendScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_legendScroll->setAutoFillBackground(false);
    if (m_legendScroll->viewport()) {
        m_legendScroll->viewport()->setAutoFillBackground(false);
    }

    m_legendContainer = new QWidget(m_legendScroll);
    m_legendContainer->setAutoFillBackground(false);
    m_legendGrid = new QGridLayout(m_legendContainer);
    m_legendGrid->setContentsMargins(2, 2, 2, 2);
    m_legendGrid->setHorizontalSpacing(6);
    m_legendGrid->setVerticalSpacing(4);

    m_legendScroll->setWidget(m_legendContainer);
    layout->addWidget(m_legendScroll, 0);

    applyVisualSettings();
}

QColor CombinedCpuWidget::threadColor(int index, int totalCount) {
    const int n = std::max(1, totalCount);
    const QColor win = QApplication::palette().color(QPalette::Window);
    const bool isDark = (win.lightnessF() < 0.5f);

    int stride = 1;
    if (n > 6) {
        stride = std::max(2, static_cast<int>(std::lround(static_cast<double>(n) * 0.382)));
        while (std::gcd(stride, n) != 1) {
            ++stride;
        }
    }

    const int slot = (index * stride) % n;
    const float hue = std::fmod(0.02f + static_cast<float>(slot) / static_cast<float>(n), 1.0f);
    const float sat = isDark ? 0.80f : 0.78f;
    float light = isDark ? 0.62f : 0.43f;

    if (!isDark && hue >= 0.12f && hue <= 0.30f) {
        light -= 0.06f;
    } else if (isDark && hue >= 0.60f && hue <= 0.75f) {
        light += 0.06f;
    }

    if (n > 12 && (index % 2) == 1) {
        light += isDark ? -0.05f : 0.05f;
    }

    light = std::clamp(light, 0.30f, 0.74f);
    return QColor::fromHslF(hue, sat, light);
}

void CombinedCpuWidget::rebuildLegend(const QVector<CoreTelemetry>& cores) {
    while (QLayoutItem* item = m_legendGrid->takeAt(0)) {
        delete item;
    }
    for (CpuLegendItem* item : m_items) {
        item->hide();
        item->deleteLater();
    }
    m_items.clear();
    m_highlightedIndex = -1;
    m_lastGridCols = 0;
    m_chart->setHighlightedSeriesIndex(-1);

    const int n = static_cast<int>(cores.size());
    if (n <= 0) {
        m_chart->clearMultiSeries();
        return;
    }

    QVector<AreaChartWidget::MultiSeriesInput> series;
    series.reserve(n);
    m_items.reserve(n);

    for (int i = 0; i < n; ++i) {
        const CoreTelemetry& core = cores[i];
        const QColor col = threadColor(i, n);

        auto* legendItem = new CpuLegendItem(i, m_legendContainer);
        legendItem->setFont(font());
        legendItem->setCoreData(core.id, col, core.usagePercent, core.curFreqMHz);
        connect(legendItem, &CpuLegendItem::hovered,
                this, &CombinedCpuWidget::onLegendHovered);
        connect(legendItem, &CpuLegendItem::unhovered,
                this, &CombinedCpuWidget::onLegendUnhovered);
        m_items.push_back(legendItem);

        AreaChartWidget::MultiSeriesInput s;
        s.id = QStringLiteral("cpu%1").arg(core.id);
        s.label = QStringLiteral("CPU %1").arg(core.id);
        s.color = col;
        s.values.push_back(std::clamp(core.usagePercent, 0.0, 100.0));
        series.push_back(std::move(s));
    }

    m_chart->setMultiSeries(series);
    reflowLegendGrid(true);
}

void CombinedCpuWidget::reflowLegendGrid(bool force) {
    if (m_items.isEmpty() || !m_legendScroll || !m_legendGrid) {
        return;
    }

    const int vpWidth = m_legendScroll->viewport() ? m_legendScroll->viewport()->width() : width();
    const int availWidth = std::max(160, (vpWidth > 40 ? vpWidth : std::max(width(), 480)) - 8);
    const int itemMinW = std::max(92, m_items.first()->minimumSizeHint().width() + 6);
    const int cols = std::clamp(availWidth / itemMinW, 1, static_cast<int>(m_items.size()));

    if (force || cols != m_lastGridCols) {
        m_lastGridCols = cols;
        while (QLayoutItem* item = m_legendGrid->takeAt(0)) {
            delete item;
        }
        for (int i = 0; i < m_items.size(); ++i) {
            m_legendGrid->addWidget(m_items[i], i / cols, i % cols);
        }
    }

    const int rows = (static_cast<int>(m_items.size()) + cols - 1) / cols;
    const int visibleRows = std::clamp(rows, 1, 4);
    const int rowH = std::max(20, m_items.first()->sizeHint().height());
    const int targetH = visibleRows * rowH + std::max(0, visibleRows - 1) * 4 + 6;
    m_legendScroll->setMaximumHeight(targetH);
    if (rows <= 4) {
        m_legendScroll->setMinimumHeight(targetH);
    } else {
        m_legendScroll->setMinimumHeight(std::min(targetH, 2 * rowH + 10));
    }
}

void CombinedCpuWidget::updateCores(const QVector<CoreTelemetry>& cores) {
    if (cores.size() != m_items.size()) {
        rebuildLegend(cores);
        return;
    }
    if (cores.isEmpty()) {
        return;
    }

    const int n = static_cast<int>(cores.size());
    QVector<double> values;
    values.reserve(n);

    for (int i = 0; i < n; ++i) {
        const CoreTelemetry& core = cores[i];
        const QColor col = m_items[i]->seriesColor().isValid()
            ? m_items[i]->seriesColor()
            : threadColor(i, n);
        m_items[i]->setCoreData(core.id, col, core.usagePercent, core.curFreqMHz);
        values.push_back(std::clamp(core.usagePercent, 0.0, 100.0));
    }

    m_chart->pushMultiSample(values);
}

void CombinedCpuWidget::updateSeriesColors() {
    const int n = static_cast<int>(m_items.size());
    if (n <= 0) {
        return;
    }
    QVector<QColor> colors;
    colors.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QColor col = threadColor(i, n);
        colors.push_back(col);
        m_items[i]->setFont(font());
        m_items[i]->setSeriesColor(col);
    }
    m_chart->setMultiSeriesColors(colors);
}

void CombinedCpuWidget::applyVisualSettings() {
    const auto& s = AppSettings::instance();
    if (m_chart) {
        m_chart->setLineWidth(s.chartLineWidth());
        m_chart->setFillOpacityPercent(s.chartFillOpacityPercent());
        m_chart->setGridVisible(s.chartShowGrid());
        m_chart->setSmoothScrollEnabled(s.chartSmoothScroll());
        m_chart->setScrollDurationMs(s.refreshIntervalMs());
        m_chart->setHoverValueVisible(s.chartShowHoverValue());
    }

    updateSeriesColors();
    reflowLegendGrid(true);
}

void CombinedCpuWidget::retranslateUi() {
    if (m_chart) {
        m_chart->update();
    }
}

void CombinedCpuWidget::installZoomFilter(QObject* filter) {
    if (!filter) {
        return;
    }
    installEventFilter(filter);
    if (m_chart) {
        m_chart->installEventFilter(filter);
    }
    if (m_legendScroll && m_legendScroll->viewport()) {
        m_legendScroll->viewport()->installEventFilter(filter);
    }
}

void CombinedCpuWidget::onLegendHovered(int index) {
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_highlightedIndex = index;
    m_chart->setHighlightedSeriesIndex(index);
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i]->setHoverStates(i == index, i != index);
    }
}

void CombinedCpuWidget::onLegendUnhovered(int index) {
    if (m_highlightedIndex != index) {
        return;
    }
    m_highlightedIndex = -1;
    m_chart->setHighlightedSeriesIndex(-1);
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i]->setHoverStates(false, false);
    }
}

void CombinedCpuWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    reflowLegendGrid(false);
}

void CombinedCpuWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange ||
        event->type() == QEvent::FontChange) {
        for (CpuLegendItem* item : m_items) {
            item->setFont(font());
        }
        updateSeriesColors();
        reflowLegendGrid(true);
    }
    QWidget::changeEvent(event);
}

} // namespace sysmon
