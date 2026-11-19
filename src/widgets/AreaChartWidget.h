#pragma once

#include "core/RingBuffer.h"

#include <QColor>
#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstddef>
#include <vector>

class QHideEvent;
class QShowEvent;
class QTimer;

namespace sysmon {

/**
 * @brief Aktif sistem paletini ve doğal arka plan saydamlığını koruyan
 *        QPainter tabanlı canlı alan grafiği bileşeni.
 */
class AreaChartWidget : public QWidget {
    Q_OBJECT

public:
    enum class DisplayMode {
        Sparkline,
        Detailed
    };

    enum class ScaleMode {
        FixedRange,
        AutoScale
    };

    struct MultiSeriesInput {
        QString id;
        QString label;
        QColor color;
        std::vector<double> values;
    };

    static constexpr std::size_t kMaxCapacity = 120;

    explicit AreaChartWidget(QWidget* parent = nullptr);

    void setDisplayMode(DisplayMode mode);
    [[nodiscard]] DisplayMode displayMode() const noexcept { return m_displayMode; }

    void setScaleMode(ScaleMode mode);
    [[nodiscard]] ScaleMode scaleMode() const noexcept { return m_scaleMode; }

    void setFixedRange(double minVal, double maxVal);
    void setAutoScaleMinCeiling(double ceiling);
    void setUnitSuffix(const QString& unit);
    void setSeriesLabels(const QString& primaryLabel, const QString& secondaryLabel = {});
    void setSecondarySeriesEnabled(bool enabled);
    void setMaxSamples(std::size_t count);
    void setCustomPrimaryColor(const QColor& color);
    void setCustomSecondaryColor(const QColor& color);
    void clearCustomColors();
    void setLineWidth(double width);
    void setFillOpacityPercent(int percent);
    void setGridVisible(bool visible);
    void setSmoothScrollEnabled(bool enabled);
    void setScrollDurationMs(int ms);
    void setHoverValueVisible(bool visible);

    void pushSample(double primaryValue);
    void pushSamples(double primaryValue, double secondaryValue);
    void setHistory(const std::vector<double>& primary,
                    const std::vector<double>& secondary = {});

    void setMultiSeries(const QVector<MultiSeriesInput>& series);
    void pushMultiSample(const QVector<double>& values);
    void clearMultiSeries();

    void clear();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onScrollTick();

private:
    struct MultiSeriesBuffer {
        QString id;
        QString label;
        QColor color;
        RingBuffer<double, kMaxCapacity> buffer;
    };

    [[nodiscard]] QColor primarySeriesColor() const;
    [[nodiscard]] QColor secondarySeriesColor() const;
    [[nodiscard]] std::size_t visibleSampleCount() const noexcept;
    [[nodiscard]] double visibleMaxSample() const noexcept;
    [[nodiscard]] double currentShiftX(const QRectF& plotRect) const noexcept;
    [[nodiscard]] QString formatValue(double value) const;
    [[nodiscard]] int computeLegendRowCount(const QRectF& plotRect) const;
    [[nodiscard]] QRectF computePlotRect(double yMax) const;
    void computeYBounds(double& yMin, double& yMax) const;
    void triggerSmoothScroll();
    void updateScrollTimerState();
    void drawGridAndAxes(QPainter& painter, const QRectF& plotRect, double yMin, double yMax) const;
    void drawLegend(QPainter& painter, const QRectF& plotRect, std::size_t count) const;
    void drawSeries(QPainter& painter,
                    const QRectF& plotRect,
                    const RingBuffer<double, kMaxCapacity>& buffer,
                    std::size_t count,
                    double yMin,
                    double yMax,
                    const QColor& color,
                    double topAlpha,
                    Qt::PenStyle penStyle) const;
    void drawHoverOverlay(QPainter& painter,
                          const QRectF& plotRect,
                          std::size_t count,
                          double yMin,
                          double yMax) const;
    void updateHoverFromPos(const QPointF& pos);

    RingBuffer<double, kMaxCapacity> m_primary;
    RingBuffer<double, kMaxCapacity> m_secondary;
    std::vector<MultiSeriesBuffer> m_multiSeries;

    DisplayMode m_displayMode{DisplayMode::Detailed};
    ScaleMode m_scaleMode{ScaleMode::FixedRange};

    double m_fixedMin{0.0};
    double m_fixedMax{100.0};
    double m_autoScaleMinCeiling{1.0};
    std::size_t m_maxSamples{60};

    QString m_unitSuffix{QStringLiteral("%")};
    QString m_primaryLabel{QStringLiteral("Value")};
    QString m_secondaryLabel;
    bool m_secondaryEnabled{false};

    QColor m_customPrimaryColor;
    QColor m_customSecondaryColor;
    double m_lineWidth{2.0};
    double m_fillTopAlpha{0.35};
    bool m_showGrid{true};
    bool m_smoothScrollEnabled{false};
    int m_scrollDurationMs{1000};
    double m_scrollLag{0.0};
    double m_measuredDurationMs{1000.0};
    QElapsedTimer m_frameClock;
    QElapsedTimer m_lastSampleClock;
    QTimer* m_scrollTimer{nullptr};

    bool m_showHoverValue{true};
    int m_hoverIndex{-1};
};

} // namespace sysmon
