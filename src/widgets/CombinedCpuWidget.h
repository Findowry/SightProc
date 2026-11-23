#pragma once

#include "core/TelemetryTypes.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QGridLayout;
class QPaintEvent;
class QResizeEvent;
class QScrollArea;

namespace sysmon {

class AreaChartWidget;

/**
 * @brief Tek bir mantıksal CPU izleği için fareyle üzerine gelindiğinde
 *        ilgili çizgiyi vurgulamaya olanak tanıyan kompakt lejant öğesi.
 */
class CpuLegendItem : public QWidget {
    Q_OBJECT

public:
    explicit CpuLegendItem(int coreIndex, QWidget* parent = nullptr);

    void setCoreData(int coreId,
                     const QColor& color,
                     double usagePercent,
                     double curFreqMHz);
    void setSeriesColor(const QColor& color);
    void setHoverStates(bool isHovered, bool isDimmed);

    [[nodiscard]] int coreIndex() const noexcept { return m_coreIndex; }
    [[nodiscard]] QColor seriesColor() const noexcept { return m_color; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void hovered(int coreIndex);
    void unhovered(int coreIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int m_coreIndex{0};
    int m_coreId{0};
    QColor m_color;
    double m_usagePercent{0.0};
    double m_freqMHz{0.0};
    bool m_hovered{false};
    bool m_dimmed{false};
};

/**
 * @brief Tüm mantıksal CPU izleklerini algısal olarak eşit dağıtılmış HSL
 *        renkleriyle tek grafikte çizen ve kompakt etkileşimli lejant sunan bileşen.
 */
class CombinedCpuWidget : public QWidget {
    Q_OBJECT

public:
    explicit CombinedCpuWidget(QWidget* parent = nullptr);

    void updateCores(const QVector<CoreTelemetry>& cores);
    void applyVisualSettings();
    void retranslateUi();
    void installZoomFilter(QObject* filter);

    [[nodiscard]] static QColor threadColor(int index, int totalCount);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onLegendHovered(int index);
    void onLegendUnhovered(int index);

private:
    void rebuildLegend(const QVector<CoreTelemetry>& cores);
    void reflowLegendGrid(bool force = false);
    void updateSeriesColors();

    AreaChartWidget* m_chart{nullptr};
    QScrollArea* m_legendScroll{nullptr};
    QWidget* m_legendContainer{nullptr};
    QGridLayout* m_legendGrid{nullptr};
    QVector<CpuLegendItem*> m_items;
    int m_highlightedIndex{-1};
    int m_lastGridCols{0};
};

} // namespace sysmon
