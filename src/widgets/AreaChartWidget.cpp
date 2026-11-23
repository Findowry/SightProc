#include "widgets/AreaChartWidget.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sysmon {

namespace {

constexpr double kMinSpan = 1e-6;

QPainterPath buildSmoothCurvePath(const std::vector<QPointF>& points) {
    QPainterPath path;
    if (points.empty()) {
        return path;
    }

    path.moveTo(points.front());
    for (std::size_t i = 1; i < points.size(); ++i) {
        const QPointF& prev = points[i - 1];
        const QPointF& curr = points[i];
        const double midX = (prev.x() + curr.x()) * 0.5;
        // Yatay teğetli kübik Bezier eğrisi değer aralığının dışına taşmayı engeller.
        path.cubicTo(QPointF(midX, prev.y()), QPointF(midX, curr.y()), curr);
    }
    return path;
}

} // namespace

AreaChartWidget::AreaChartWidget(QWidget* parent)
    : QWidget(parent) {
    // Wayland alt bileşenlerinde arka planı doğal Kvantum yüzeyine bırakmak için
    // yalnızca otomatik arka plan doldurmayı devre dışı bırakıyoruz.
    setAutoFillBackground(false);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_scrollTimer = new QTimer(this);
    m_scrollTimer->setTimerType(Qt::PreciseTimer);
    m_scrollTimer->setInterval(16);
    connect(m_scrollTimer, &QTimer::timeout, this, &AreaChartWidget::onScrollTick);
}

void AreaChartWidget::setDisplayMode(DisplayMode mode) {
    if (m_displayMode == mode) {
        return;
    }
    m_displayMode = mode;
    m_hoverIndex = -1;
    updateScrollTimerState();
    updateGeometry();
    update();
}

void AreaChartWidget::setScaleMode(ScaleMode mode) {
    if (m_scaleMode == mode) {
        return;
    }
    m_scaleMode = mode;
    update();
}

void AreaChartWidget::setFixedRange(double minVal, double maxVal) {
    const double clampedMax = std::max(maxVal, minVal + kMinSpan);
    if (qFuzzyCompare(m_fixedMin, minVal) && qFuzzyCompare(m_fixedMax, clampedMax)) {
        return;
    }
    m_fixedMin = minVal;
    m_fixedMax = clampedMax;
    update();
}

void AreaChartWidget::setAutoScaleMinCeiling(double ceiling) {
    const double clamped = std::max(ceiling, kMinSpan);
    if (qFuzzyCompare(m_autoScaleMinCeiling, clamped)) {
        return;
    }
    m_autoScaleMinCeiling = clamped;
    update();
}

void AreaChartWidget::setUnitSuffix(const QString& unit) {
    if (m_unitSuffix == unit) {
        return;
    }
    m_unitSuffix = unit;
    update();
}

void AreaChartWidget::setSeriesLabels(const QString& primaryLabel, const QString& secondaryLabel) {
    if (m_primaryLabel == primaryLabel && m_secondaryLabel == secondaryLabel) {
        return;
    }
    m_primaryLabel = primaryLabel;
    m_secondaryLabel = secondaryLabel;
    update();
}

void AreaChartWidget::setSecondarySeriesEnabled(bool enabled) {
    if (m_secondaryEnabled == enabled) {
        return;
    }
    m_secondaryEnabled = enabled;
    update();
}

void AreaChartWidget::setMaxSamples(std::size_t count) {
    const std::size_t clamped = std::clamp<std::size_t>(count, 2, kMaxCapacity - 2);
    if (m_maxSamples == clamped) {
        return;
    }
    m_maxSamples = clamped;
    m_hoverIndex = -1;
    update();
}

void AreaChartWidget::setCustomPrimaryColor(const QColor& color) {
    if (m_customPrimaryColor == color) {
        return;
    }
    m_customPrimaryColor = color;
    update();
}

void AreaChartWidget::setCustomSecondaryColor(const QColor& color) {
    if (m_customSecondaryColor == color) {
        return;
    }
    m_customSecondaryColor = color;
    update();
}

void AreaChartWidget::clearCustomColors() {
    if (!m_customPrimaryColor.isValid() && !m_customSecondaryColor.isValid()) {
        return;
    }
    QColor empty;
    m_customPrimaryColor = empty;
    m_customSecondaryColor = empty;
    update();
}

void AreaChartWidget::setLineWidth(double width) {
    const double clamped = std::clamp(width, 1.0, 4.0);
    if (qFuzzyCompare(m_lineWidth, clamped)) {
        return;
    }
    m_lineWidth = clamped;
    update();
}

void AreaChartWidget::setFillOpacityPercent(int percent) {
    const double alpha = std::clamp(static_cast<double>(percent) / 100.0, 0.05, 0.85);
    if (qFuzzyCompare(m_fillTopAlpha, alpha)) {
        return;
    }
    m_fillTopAlpha = alpha;
    update();
}

void AreaChartWidget::setGridVisible(bool visible) {
    if (m_showGrid == visible) {
        return;
    }
    m_showGrid = visible;
    update();
}

void AreaChartWidget::setSmoothScrollEnabled(bool enabled) {
    if (m_smoothScrollEnabled == enabled) {
        return;
    }
    m_smoothScrollEnabled = enabled;
    if (!enabled) {
        m_scrollLag = 0.0;
        m_frameClock.invalidate();
        m_lastSampleClock.invalidate();
    }
    updateScrollTimerState();
    update();
}

void AreaChartWidget::setScrollDurationMs(int ms) {
    const int clamped = std::clamp(ms, 250, 5000);
    if (m_scrollDurationMs == clamped) {
        return;
    }
    m_scrollDurationMs = clamped;
    m_measuredDurationMs = static_cast<double>(clamped);
    m_lastSampleClock.invalidate();
}

void AreaChartWidget::setHoverValueVisible(bool visible) {
    if (m_showHoverValue == visible) {
        return;
    }
    m_showHoverValue = visible;
    update();
}

void AreaChartWidget::triggerSmoothScroll() {
    if (!m_smoothScrollEnabled) {
        m_scrollLag = 0.0;
        return;
    }

    if (m_lastSampleClock.isValid()) {
        const double sampleDt = static_cast<double>(m_lastSampleClock.restart());
        if (sampleDt >= m_scrollDurationMs * 0.6 && sampleDt <= m_scrollDurationMs * 1.5) {
            m_measuredDurationMs = m_measuredDurationMs * 0.75 + sampleDt * 0.25;
        } else {
            m_measuredDurationMs = static_cast<double>(m_scrollDurationMs);
        }
    } else {
        m_lastSampleClock.start();
        m_measuredDurationMs = static_cast<double>(m_scrollDurationMs);
    }

    m_scrollLag = std::min(m_scrollLag + 1.0, 2.0);
    if (!m_scrollTimer->isActive() || !m_frameClock.isValid()) {
        m_frameClock.restart();
    }
    updateScrollTimerState();
}

void AreaChartWidget::updateScrollTimerState() {
    if (!m_scrollTimer) {
        return;
    }
    const bool shouldRun = m_smoothScrollEnabled
        && isVisible()
        && m_scrollLag > 0.0
        && visibleSampleCount() > 1;
    if (shouldRun) {
        if (!m_scrollTimer->isActive()) {
            m_frameClock.restart();
            m_scrollTimer->start();
        }
    } else if (m_scrollTimer->isActive()) {
        m_scrollTimer->stop();
    }
}

void AreaChartWidget::onScrollTick() {
    if (!m_smoothScrollEnabled) {
        m_scrollLag = 0.0;
        updateScrollTimerState();
        update();
        return;
    }

    const double dtMs = m_frameClock.isValid()
        ? std::clamp(static_cast<double>(m_frameClock.restart()), 0.0, 100.0)
        : 16.0;
    const double durMs = std::max(100.0, m_measuredDurationMs);
    double speed = 1.0;
    if (m_scrollLag > 1.05) {
        speed = 1.0 + (m_scrollLag - 1.05) * 1.5;
    } else if (m_scrollLag < 0.08) {
        speed = 0.65;
    }

    m_scrollLag = std::max(0.0, m_scrollLag - (dtMs / durMs) * speed);
    if (m_scrollLag <= 1e-4) {
        m_scrollLag = 0.0;
        updateScrollTimerState();
    }
    update();
}

void AreaChartWidget::pushSample(double primaryValue) {
    m_multiSeries.clear();
    m_primary.push(primaryValue);
    m_secondary.push(0.0);
    triggerSmoothScroll();
    update();
}

void AreaChartWidget::pushSamples(double primaryValue, double secondaryValue) {
    m_multiSeries.clear();
    m_primary.push(primaryValue);
    m_secondary.push(secondaryValue);
    triggerSmoothScroll();
    update();
}

void AreaChartWidget::setHistory(const std::vector<double>& primary,
                                 const std::vector<double>& secondary) {
    m_multiSeries.clear();
    m_primary.clear();
    m_secondary.clear();
    m_hoverIndex = -1;
    m_highlightedSeriesIndex = -1;
    m_scrollLag = 0.0;
    m_frameClock.invalidate();
    m_lastSampleClock.invalidate();
    updateScrollTimerState();

    const std::size_t count = primary.size();
    const std::size_t start = (count > kMaxCapacity) ? (count - kMaxCapacity) : 0;
    for (std::size_t i = start; i < count; ++i) {
        m_primary.push(primary[i]);
        const double sec = (i < secondary.size()) ? secondary[i] : 0.0;
        m_secondary.push(sec);
    }
    update();
}

void AreaChartWidget::setMultiSeries(const QVector<MultiSeriesInput>& series) {
    m_multiSeries.clear();
    m_multiSeries.reserve(static_cast<std::size_t>(series.size()));
    for (const auto& s : series) {
        MultiSeriesBuffer item;
        item.id = s.id;
        item.label = s.label;
        item.color = s.color;
        const std::size_t count = s.values.size();
        const std::size_t start = (count > kMaxCapacity) ? (count - kMaxCapacity) : 0;
        for (std::size_t i = start; i < count; ++i) {
            item.buffer.push(s.values[i]);
        }
        m_multiSeries.push_back(std::move(item));
    }
    m_hoverIndex = -1;
    if (m_highlightedSeriesIndex >= static_cast<int>(m_multiSeries.size())) {
        m_highlightedSeriesIndex = -1;
    }
    m_scrollLag = 0.0;
    m_frameClock.invalidate();
    m_lastSampleClock.invalidate();
    updateScrollTimerState();
    update();
}

void AreaChartWidget::pushMultiSample(const QVector<double>& values) {
    if (m_multiSeries.size() != static_cast<std::size_t>(values.size()) || m_multiSeries.empty()) {
        return;
    }
    for (std::size_t i = 0; i < m_multiSeries.size(); ++i) {
        m_multiSeries[i].buffer.push(values[static_cast<qsizetype>(i)]);
    }
    triggerSmoothScroll();
    update();
}

void AreaChartWidget::clearMultiSeries() {
    m_multiSeries.clear();
    m_hoverIndex = -1;
    m_highlightedSeriesIndex = -1;
    update();
}

void AreaChartWidget::setInternalLegendVisible(bool visible) {
    if (m_internalLegendVisible == visible) {
        return;
    }
    m_internalLegendVisible = visible;
    updateGeometry();
    update();
}

void AreaChartWidget::setMultiSeriesFillEnabled(bool enabled) {
    if (m_multiSeriesFillEnabled == enabled) {
        return;
    }
    m_multiSeriesFillEnabled = enabled;
    update();
}

void AreaChartWidget::setHighlightedSeriesIndex(int index) {
    if (m_highlightedSeriesIndex == index) {
        return;
    }
    m_highlightedSeriesIndex = index;
    update();
}

void AreaChartWidget::setMultiSeriesColors(const QVector<QColor>& colors) {
    const std::size_t n = std::min(m_multiSeries.size(), static_cast<std::size_t>(colors.size()));
    bool changed = false;
    for (std::size_t i = 0; i < n; ++i) {
        const QColor& c = colors[static_cast<qsizetype>(i)];
        if (m_multiSeries[i].color != c) {
            m_multiSeries[i].color = c;
            changed = true;
        }
    }
    if (changed) {
        update();
    }
}

void AreaChartWidget::clear() {
    m_primary.clear();
    m_secondary.clear();
    m_multiSeries.clear();
    m_hoverIndex = -1;
    m_highlightedSeriesIndex = -1;
    m_scrollLag = 0.0;
    m_frameClock.invalidate();
    m_lastSampleClock.invalidate();
    updateScrollTimerState();
    update();
}

QSize AreaChartWidget::sizeHint() const {
    if (m_displayMode == DisplayMode::Sparkline) {
        return QSize(84, 30);
    }
    return QSize(420, 220);
}

QSize AreaChartWidget::minimumSizeHint() const {
    if (m_displayMode == DisplayMode::Sparkline) {
        return QSize(44, 20);
    }
    return QSize(180, 96);
}

void AreaChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double yMin = 0.0;
    double yMax = 100.0;
    computeYBounds(yMin, yMax);

    const QRectF plotRect = computePlotRect(yMax);
    if (plotRect.width() < 4.0 || plotRect.height() < 4.0) {
        return;
    }

    const std::size_t count = visibleSampleCount();

    if (m_displayMode == DisplayMode::Detailed) {
        drawGridAndAxes(painter, plotRect, yMin, yMax);
        if (m_internalLegendVisible) {
            drawLegend(painter, plotRect, count);
        }
    }

    if (count > 0) {
        painter.save();
        painter.setClipRect(plotRect.adjusted(-1.0, -2.0, 1.0, 2.0));

        if (!m_multiSeries.empty()) {
            const bool hasHighlight =
                (m_highlightedSeriesIndex >= 0 &&
                 static_cast<std::size_t>(m_highlightedSeriesIndex) < m_multiSeries.size());

            for (std::size_t i = 0; i < m_multiSeries.size(); ++i) {
                if (hasHighlight && static_cast<int>(i) == m_highlightedSeriesIndex) {
                    continue;
                }
                const auto& s = m_multiSeries[i];
                const std::size_t sc = std::min(s.buffer.size(), count);
                if (sc == 0) {
                    continue;
                }
                QColor col = s.color.isValid() ? s.color : primarySeriesColor();
                if (hasHighlight) {
                    col.setAlphaF(0.18f);
                    drawSeries(painter,
                               plotRect,
                               s.buffer,
                               sc,
                               yMin,
                               yMax,
                               col,
                               0.0,
                               Qt::SolidLine,
                               std::max(1.0, m_lineWidth * 0.8));
                } else if (!m_multiSeriesFillEnabled) {
                    col.setAlphaF(0.90f);
                    const double lw = (m_multiSeries.size() >= 12)
                        ? std::min(m_lineWidth, 1.65)
                        : m_lineWidth;
                    drawSeries(painter,
                               plotRect,
                               s.buffer,
                               sc,
                               yMin,
                               yMax,
                               col,
                               0.0,
                               Qt::SolidLine,
                               lw);
                } else {
                    const double fillAlpha = std::clamp(m_fillTopAlpha * 0.45, 0.04, 0.35);
                    drawSeries(painter,
                               plotRect,
                               s.buffer,
                               sc,
                               yMin,
                               yMax,
                               col,
                               fillAlpha,
                               Qt::SolidLine);
                }
            }

            if (hasHighlight) {
                const auto& hs = m_multiSeries[static_cast<std::size_t>(m_highlightedSeriesIndex)];
                const std::size_t sc = std::min(hs.buffer.size(), count);
                if (sc > 0) {
                    QColor col = hs.color.isValid() ? hs.color : primarySeriesColor();
                    col.setAlphaF(1.0f);
                    const double highlightFill = std::clamp(m_fillTopAlpha * 0.55, 0.10, 0.32);
                    const double highlightWidth = std::min(4.0, m_lineWidth + 1.1);
                    drawSeries(painter,
                               plotRect,
                               hs.buffer,
                               sc,
                               yMin,
                               yMax,
                               col,
                               highlightFill,
                               Qt::SolidLine,
                               highlightWidth);
                }
            }
        } else {
            if (m_secondaryEnabled) {
                drawSeries(painter,
                           plotRect,
                           m_secondary,
                           count,
                           yMin,
                           yMax,
                           secondarySeriesColor(),
                           std::clamp(m_fillTopAlpha * 0.65, 0.05, 0.60),
                           Qt::DashLine);
            }

            drawSeries(painter,
                       plotRect,
                       m_primary,
                       count,
                       yMin,
                       yMax,
                       primarySeriesColor(),
                       m_fillTopAlpha,
                       Qt::SolidLine);
        }

        if (m_displayMode == DisplayMode::Detailed) {
            drawHoverOverlay(painter, plotRect, count, yMin, yMax);
        }

        painter.restore();
    }
}

void AreaChartWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        update();
    }
    QWidget::changeEvent(event);
}

void AreaChartWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_displayMode == DisplayMode::Detailed) {
        updateHoverFromPos(event->position());
    }
    QWidget::mouseMoveEvent(event);
}

void AreaChartWidget::leaveEvent(QEvent* event) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void AreaChartWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateScrollTimerState();
}

void AreaChartWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    updateScrollTimerState();
}

QColor AreaChartWidget::primarySeriesColor() const {
    if (m_customPrimaryColor.isValid()) {
        return m_customPrimaryColor;
    }
    return QApplication::palette().color(QPalette::Highlight);
}

QColor AreaChartWidget::secondarySeriesColor() const {
    if (m_customSecondaryColor.isValid()) {
        return m_customSecondaryColor;
    }
    const QColor primary = primarySeriesColor();
    const QColor link = QApplication::palette().color(QPalette::Link);

    float ph = 0.0f;
    float ps = 0.0f;
    float pl = 0.0f;
    float lh = 0.0f;
    float ls = 0.0f;
    float ll = 0.0f;
    primary.getHslF(&ph, &ps, &pl);
    link.getHslF(&lh, &ls, &ll);

    if (ph < 0.0f) {
        ph = 0.55f;
    }

    const float rawDiff = std::fabs(ph - lh);
    const float hueDistance = std::min(rawDiff, 1.0f - rawDiff);
    if (lh >= 0.0f && hueDistance >= 0.14f) {
        return link;
    }

    const float shiftedHue = std::fmod(ph + 0.35f, 1.0f);
    const float sat = std::clamp(ps > 0.1f ? ps : 0.65f, 0.35f, 0.85f);
    const float light = std::clamp(pl, 0.35f, 0.65f);
    return QColor::fromHslF(shiftedHue, sat, light);
}

std::size_t AreaChartWidget::visibleSampleCount() const noexcept {
    const std::size_t cap = (m_smoothScrollEnabled && m_scrollLag > 0.0)
        ? (m_maxSamples + 2)
        : m_maxSamples;
    if (!m_multiSeries.empty()) {
        return std::min(m_multiSeries.front().buffer.size(), cap);
    }
    return std::min(m_primary.size(), cap);
}

double AreaChartWidget::visibleMaxSample() const noexcept {
    const std::size_t count = visibleSampleCount();
    if (count == 0) {
        return 0.0;
    }
    double best = 0.0;
    if (!m_multiSeries.empty()) {
        for (const auto& s : m_multiSeries) {
            const std::size_t sc = std::min(s.buffer.size(), count);
            const std::size_t off = s.buffer.size() - sc;
            for (std::size_t i = 0; i < sc; ++i) {
                best = std::max(best, s.buffer[off + i]);
            }
        }
        return best;
    }
    const std::size_t offset = m_primary.size() - count;
    for (std::size_t i = 0; i < count; ++i) {
        best = std::max(best, m_primary[offset + i]);
        if (m_secondaryEnabled) {
            best = std::max(best, m_secondary[offset + i]);
        }
    }
    return best;
}

double AreaChartWidget::currentShiftX(const QRectF& plotRect) const noexcept {
    if (!m_smoothScrollEnabled || m_scrollLag <= 0.0) {
        return 0.0;
    }
    const double stepX = (m_maxSamples > 1)
        ? (plotRect.width() / static_cast<double>(m_maxSamples - 1))
        : plotRect.width();
    return m_scrollLag * stepX;
}

QString AreaChartWidget::formatValue(double value) const {
    const int decimals = (std::fabs(value) >= 100.0 || m_unitSuffix == QStringLiteral("%")) ? 1 : 2;
    const QString num = QString::number(value, 'f', decimals);
    if (m_unitSuffix.isEmpty()) {
        return num;
    }
    if (m_unitSuffix == QStringLiteral("%")) {
        return num + m_unitSuffix;
    }
    return num + QLatin1Char(' ') + m_unitSuffix;
}

int AreaChartWidget::computeLegendRowCount(const QRectF& baseRect) const {
    if (m_multiSeries.size() <= 1) {
        return 1;
    }
    const QFontMetrics fm(font());
    const double maxW = std::max(120.0, baseRect.width());
    double cursorX = 0.0;
    int rows = 1;
    for (const auto& s : m_multiSeries) {
        const QString sampleText = QStringLiteral("%1: 999.9 %2").arg(s.label, m_unitSuffix);
        const double itemW = 22.0 + static_cast<double>(fm.horizontalAdvance(sampleText)) + 18.0;
        if (cursorX > 0.0 && cursorX + itemW > maxW) {
            ++rows;
            cursorX = 0.0;
            if (rows >= 3) {
                break;
            }
        }
        cursorX += itemW;
    }
    return rows;
}

QRectF AreaChartWidget::computePlotRect(double yMax) const {
    if (m_displayMode == DisplayMode::Sparkline) {
        return QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
    }

    const QFontMetrics fm(font());
    const double lineH = static_cast<double>(fm.height()) + 4.0;
    const double yLabelWidth = static_cast<double>(fm.horizontalAdvance(formatValue(yMax))) + 14.0;
    const double leftMargin = std::max(54.0, yLabelWidth);
    if (!m_internalLegendVisible) {
        return QRectF(leftMargin,
                      10.0,
                      std::max(0.0, static_cast<double>(width()) - leftMargin - 12.0),
                      std::max(0.0, static_cast<double>(height()) - 22.0));
    }
    const QRectF rough(leftMargin,
                       lineH,
                       std::max(0.0, static_cast<double>(width()) - leftMargin - 12.0),
                       std::max(0.0, static_cast<double>(height()) - lineH - 12.0));
    const int rows = computeLegendRowCount(rough);
    const double legendHeight = lineH * rows + 8.0;
    return QRectF(leftMargin,
                  legendHeight,
                  std::max(0.0, static_cast<double>(width()) - leftMargin - 12.0),
                  std::max(0.0, static_cast<double>(height()) - legendHeight - 12.0));
}

void AreaChartWidget::computeYBounds(double& yMin, double& yMax) const {
    if (m_scaleMode == ScaleMode::FixedRange) {
        yMin = m_fixedMin;
        yMax = m_fixedMax;
        return;
    }

    yMin = 0.0;
    const double peak = visibleMaxSample();
    yMax = std::max(m_autoScaleMinCeiling, peak * 1.15);
}

void AreaChartWidget::drawGridAndAxes(QPainter& painter,
                                      const QRectF& plotRect,
                                      double yMin,
                                      double yMax) const {
    const QPalette pal = QApplication::palette();

    QColor gridColor = pal.color(QPalette::Mid);
    gridColor.setAlphaF(0.25f);

    QColor borderColor = pal.color(QPalette::Mid);
    borderColor.setAlphaF(0.45f);

    const QColor axisTextColor = pal.color(QPalette::PlaceholderText);
    const QFontMetrics fm(font());

    constexpr int kHorizontalDivs = 4;
    constexpr int kVerticalDivs = 6;

    painter.save();
    if (m_showGrid) {
        painter.setPen(QPen(gridColor, 1.0));
        for (int i = 1; i < kVerticalDivs; ++i) {
            const double x = plotRect.left() + (plotRect.width() * i) / kVerticalDivs;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
    }

    for (int i = 0; i <= kHorizontalDivs; ++i) {
        const double ratio = static_cast<double>(i) / kHorizontalDivs;
        const double y = plotRect.top() + plotRect.height() * ratio;
        if (m_showGrid && i > 0 && i < kHorizontalDivs) {
            painter.setPen(QPen(gridColor, 1.0));
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        if (i == 0 || i == kHorizontalDivs / 2 || i == kHorizontalDivs) {
            const double val = yMax - (yMax - yMin) * ratio;
            const QString label = formatValue(val);
            const QRectF labelRect(2.0,
                                   y - fm.height() * 0.5,
                                   plotRect.left() - 8.0,
                                   static_cast<double>(fm.height()));
            painter.setPen(axisTextColor);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

    painter.setPen(QPen(borderColor, 1.0));
    painter.drawRect(plotRect);
    painter.restore();
}

void AreaChartWidget::drawLegend(QPainter& painter,
                                 const QRectF& plotRect,
                                 std::size_t count) const {
    const QPalette pal = QApplication::palette();
    const QFontMetrics fm(font());
    double cursorY = 4.0;
    const double lineHeight = static_cast<double>(fm.height()) + 4.0;
    double cursorX = plotRect.left();

    auto resolveHoverIdx = [&](std::size_t bufSize) -> int {
        if (count == 0 || bufSize == 0) {
            return -1;
        }
        const std::size_t sc = std::min(bufSize, count);
        const std::size_t offset = bufSize - sc;
        if (m_hoverIndex >= 0 && static_cast<std::size_t>(m_hoverIndex) < sc) {
            return static_cast<int>(offset + static_cast<std::size_t>(m_hoverIndex));
        }
        return static_cast<int>(bufSize - 1);
    };

    auto drawEntry = [&](const QColor& color,
                         Qt::PenStyle style,
                         const QString& label,
                         double value,
                         bool hasValue) {
        const QString text = hasValue
            ? QStringLiteral("%1: %2").arg(label, formatValue(value))
            : label;
        const int textWidth = fm.horizontalAdvance(text);
        const double itemWidth = 22.0 + static_cast<double>(textWidth) + 18.0;
        if (cursorX > plotRect.left() + 1.0 && cursorX + itemWidth > plotRect.right()) {
            cursorX = plotRect.left();
            cursorY += lineHeight;
        }

        const double swatchY = cursorY + lineHeight * 0.5;
        QPen swatchPen(color, 2.0, style, Qt::RoundCap);
        painter.setPen(swatchPen);
        painter.drawLine(QPointF(cursorX, swatchY), QPointF(cursorX + 16.0, swatchY));
        cursorX += 22.0;

        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(QRectF(cursorX, cursorY, static_cast<double>(textWidth) + 4.0, lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         text);
        cursorX += static_cast<double>(textWidth) + 18.0;
    };

    painter.save();
    if (!m_multiSeries.empty()) {
        for (const auto& s : m_multiSeries) {
            const int sIdx = resolveHoverIdx(s.buffer.size());
            const bool hasVal = sIdx >= 0;
            const double val = hasVal ? s.buffer[static_cast<std::size_t>(sIdx)] : 0.0;
            drawEntry(s.color.isValid() ? s.color : primarySeriesColor(),
                      Qt::SolidLine,
                      s.label,
                      val,
                      hasVal);
        }
        painter.restore();
        return;
    }

    const int sampleIdx = resolveHoverIdx(m_primary.size());
    const bool hasVal = sampleIdx >= 0;
    const double primaryVal = hasVal ? m_primary[static_cast<std::size_t>(sampleIdx)] : 0.0;
    drawEntry(primarySeriesColor(), Qt::SolidLine, m_primaryLabel, primaryVal, hasVal);

    if (m_secondaryEnabled && !m_secondaryLabel.isEmpty()) {
        const double secondaryVal = hasVal ? m_secondary[static_cast<std::size_t>(sampleIdx)] : 0.0;
        drawEntry(secondarySeriesColor(), Qt::DashLine, m_secondaryLabel, secondaryVal, hasVal);
    }
    painter.restore();
}

void AreaChartWidget::drawSeries(QPainter& painter,
                                 const QRectF& plotRect,
                                 const RingBuffer<double, kMaxCapacity>& buffer,
                                 std::size_t count,
                                 double yMin,
                                 double yMax,
                                 const QColor& color,
                                 double topAlpha,
                                 Qt::PenStyle penStyle,
                                 double strokeWidthOverride) const {
    const std::size_t offset = buffer.size() - count;
    const double spanY = std::max(yMax - yMin, kMinSpan);
    const double stepX = (m_maxSamples > 1)
        ? (plotRect.width() / static_cast<double>(m_maxSamples - 1))
        : plotRect.width();
    const double shiftX = currentShiftX(plotRect);

    std::vector<QPointF> points;
    points.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const double val = std::clamp(buffer[offset + i], yMin, yMax);
        const double normY = (val - yMin) / spanY;
        const double x = plotRect.right() - static_cast<double>(count - 1 - i) * stepX + shiftX;
        const double y = plotRect.bottom() - normY * plotRect.height();
        points.emplace_back(x, y);
    }

    if (points.size() == 1) {
        const QPointF& p = points.front();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(p, 2.5, 2.5);
        return;
    }

    const QPainterPath strokePath = buildSmoothCurvePath(points);

    if (topAlpha > 0.001) {
        QPainterPath fillPath = strokePath;
        fillPath.lineTo(points.back().x(), plotRect.bottom());
        fillPath.lineTo(points.front().x(), plotRect.bottom());
        fillPath.closeSubpath();

        QColor topColor = color;
        topColor.setAlphaF(static_cast<float>(topAlpha));
        QColor bottomColor = color;
        bottomColor.setAlphaF(0.02f);

        QLinearGradient gradient(0.0, plotRect.top(), 0.0, plotRect.bottom());
        gradient.setColorAt(0.0, topColor);
        gradient.setColorAt(1.0, bottomColor);

        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawPath(fillPath);
    }

    const double baseWidth = (strokeWidthOverride > 0.0) ? strokeWidthOverride : m_lineWidth;
    const double penWidth = (m_displayMode == DisplayMode::Sparkline)
        ? std::max(1.2, baseWidth * 0.75)
        : baseWidth;
    QPen strokePen(color, penWidth, penStyle, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(strokePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(strokePath);
}

void AreaChartWidget::drawHoverOverlay(QPainter& painter,
                                       const QRectF& plotRect,
                                       std::size_t count,
                                       double yMin,
                                       double yMax) const {
    if (m_hoverIndex < 0 || static_cast<std::size_t>(m_hoverIndex) >= count) {
        return;
    }

    const std::size_t idx = static_cast<std::size_t>(m_hoverIndex);
    const double spanY = std::max(yMax - yMin, kMinSpan);
    const double stepX = (m_maxSamples > 1)
        ? (plotRect.width() / static_cast<double>(m_maxSamples - 1))
        : plotRect.width();
    const double shiftX = currentShiftX(plotRect);

    const double x = plotRect.right() - static_cast<double>(count - 1 - idx) * stepX + shiftX;
    if (x < plotRect.left() - 2.0 || x > plotRect.right() + 2.0) {
        return;
    }

    const QPalette pal = QApplication::palette();
    QColor guideColor = pal.color(QPalette::Mid);
    guideColor.setAlphaF(0.55f);

    painter.save();
    painter.setPen(QPen(guideColor, 1.0, Qt::DotLine));
    painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));

    struct CalloutRow {
        QColor color;
        QString text;
        double y{0.0};
    };
    std::vector<CalloutRow> rows;

    auto addPoint = [&](double rawVal, const QColor& color, const QString& label) {
        const double val = std::clamp(rawVal, yMin, yMax);
        const double normY = (val - yMin) / spanY;
        const double y = plotRect.bottom() - normY * plotRect.height();
        painter.setPen(QPen(pal.color(QPalette::Window), 1.5));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(x, y), 4.0, 4.0);

        const QString valStr = formatValue(rawVal);
        const QString text = label.isEmpty() ? valStr : QStringLiteral("%1: %2").arg(label, valStr);
        rows.push_back(CalloutRow{color, text, y});
    };

    if (!m_multiSeries.empty()) {
        const bool hasHighlight =
            (m_highlightedSeriesIndex >= 0 &&
             static_cast<std::size_t>(m_highlightedSeriesIndex) < m_multiSeries.size());
        if (hasHighlight) {
            const auto& s = m_multiSeries[static_cast<std::size_t>(m_highlightedSeriesIndex)];
            const std::size_t sc = std::min(s.buffer.size(), count);
            if (idx < sc) {
                const std::size_t off = s.buffer.size() - sc;
                const QColor col = s.color.isValid() ? s.color : primarySeriesColor();
                addPoint(s.buffer[off + idx], col, s.label);
            }
        } else if (m_multiSeries.size() <= 8) {
            for (const auto& s : m_multiSeries) {
                const std::size_t sc = std::min(s.buffer.size(), count);
                if (idx >= sc) {
                    continue;
                }
                const std::size_t off = s.buffer.size() - sc;
                const QColor col = s.color.isValid() ? s.color : primarySeriesColor();
                addPoint(s.buffer[off + idx], col, s.label);
            }
        } else {
            struct Candidate {
                std::size_t seriesIdx;
                double rawVal;
                double pixelY;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(m_multiSeries.size());
            std::size_t closestIdx = 0;
            double minCursorDist = 1e9;

            for (std::size_t i = 0; i < m_multiSeries.size(); ++i) {
                const auto& s = m_multiSeries[i];
                const std::size_t sc = std::min(s.buffer.size(), count);
                if (idx >= sc) {
                    continue;
                }
                const std::size_t off = s.buffer.size() - sc;
                const double rawVal = s.buffer[off + idx];
                const double clamped = std::clamp(rawVal, yMin, yMax);
                const double pixelY = plotRect.bottom() - ((clamped - yMin) / spanY) * plotRect.height();
                const double dist = std::fabs(pixelY - m_hoverPos.y());
                if (candidates.empty() || dist < minCursorDist) {
                    minCursorDist = dist;
                    closestIdx = i;
                }
                candidates.push_back(Candidate{i, rawVal, pixelY});
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                          if (a.rawVal != b.rawVal) {
                              return a.rawVal > b.rawVal;
                          }
                          return a.seriesIdx < b.seriesIdx;
                      });

            std::vector<Candidate> selected;
            selected.reserve(6);
            bool closestIncluded = false;
            for (const auto& c : candidates) {
                if (selected.size() >= 5) {
                    break;
                }
                selected.push_back(c);
                if (c.seriesIdx == closestIdx) {
                    closestIncluded = true;
                }
            }
            if (!closestIncluded) {
                for (const auto& c : candidates) {
                    if (c.seriesIdx == closestIdx) {
                        selected.push_back(c);
                        break;
                    }
                }
            }

            for (const auto& c : selected) {
                const auto& s = m_multiSeries[c.seriesIdx];
                const QColor col = s.color.isValid() ? s.color : primarySeriesColor();
                addPoint(c.rawVal, col, s.label);
            }
        }
    } else {
        const std::size_t offset = m_primary.size() - count;
        if (m_secondaryEnabled) {
            addPoint(m_secondary[offset + idx], secondarySeriesColor(), m_secondaryLabel);
        }
        addPoint(m_primary[offset + idx], primarySeriesColor(), m_primaryLabel);
    }

    if (m_showHoverValue && !rows.empty()) {
        const QFontMetrics fm(font());
        const double padX = 8.0;
        const double padY = 5.0;
        const double rowH = static_cast<double>(fm.height()) + 2.0;
        double maxTextW = 0.0;
        for (const auto& r : rows) {
            maxTextW = std::max(maxTextW, static_cast<double>(fm.horizontalAdvance(r.text)));
        }
        const double swatchSpace = 12.0;
        const double boxW = padX * 2.0 + swatchSpace + maxTextW;
        const double boxH = padY * 2.0 + rowH * static_cast<double>(rows.size());

        const double anchorY = rows.back().y;
        double boxX = x + 12.0;
        if (boxX + boxW > plotRect.right() - 4.0) {
            boxX = x - 12.0 - boxW;
        }
        boxX = std::clamp(boxX, plotRect.left() + 4.0, std::max(plotRect.left() + 4.0, plotRect.right() - boxW - 4.0));

        double boxY = anchorY - boxH * 0.5;
        boxY = std::clamp(boxY, plotRect.top() + 4.0, std::max(plotRect.top() + 4.0, plotRect.bottom() - boxH - 4.0));

        const QRectF boxRect(boxX, boxY, boxW, boxH);
        QColor bgCol = pal.color(QPalette::ToolTipBase);
        bgCol.setAlphaF(0.92f);
        QColor borderCol = primarySeriesColor();
        borderCol.setAlphaF(0.65f);

        painter.setPen(QPen(borderCol, 1.0));
        painter.setBrush(bgCol);
        painter.drawRoundedRect(boxRect, 5.0, 5.0);

        for (std::size_t rIdx = 0; rIdx < rows.size(); ++rIdx) {
            const auto& row = rows[rIdx];
            const double ry = boxY + padY + rowH * static_cast<double>(rIdx);
            painter.setPen(Qt::NoPen);
            painter.setBrush(row.color);
            painter.drawEllipse(QPointF(boxX + padX + 3.5, ry + rowH * 0.5), 3.0, 3.0);

            painter.setPen(pal.color(QPalette::ToolTipText));
            painter.drawText(QRectF(boxX + padX + swatchSpace, ry, maxTextW + 2.0, rowH),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             row.text);
        }
    }

    painter.restore();
}

void AreaChartWidget::updateHoverFromPos(const QPointF& pos) {
    double yMin = 0.0;
    double yMax = 100.0;
    computeYBounds(yMin, yMax);
    const QRectF plotRect = computePlotRect(yMax);
    const std::size_t count = visibleSampleCount();

    if (count == 0 || !plotRect.contains(pos)) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        return;
    }

    m_hoverPos = pos;

    const double stepX = (m_maxSamples > 1)
        ? (plotRect.width() / static_cast<double>(m_maxSamples - 1))
        : plotRect.width();
    const double shiftX = currentShiftX(plotRect);
    const double distFromRight = (plotRect.right() + shiftX) - pos.x();
    const int stepsFromEnd = static_cast<int>(std::llround(distFromRight / stepX));
    const int newIndex = static_cast<int>(count) - 1 - stepsFromEnd;

    if (newIndex >= 0 && newIndex < static_cast<int>(count)) {
        if (m_hoverIndex != newIndex ||
            (m_showHoverValue && m_multiSeries.size() > 8 && m_highlightedSeriesIndex < 0)) {
            m_hoverIndex = newIndex;
            update();
        }
    } else if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
}

} // namespace sysmon
