#pragma once

#include "core/TelemetryTypes.h"

#include <QScrollArea>
#include <QVector>

class QGridLayout;
class QLabel;

namespace sysmon {

class AreaChartWidget;

/**
 * @brief CPU seçiliyken tüm mantıksal çekirdekleri duyarlı ızgara düzeninde
 *        bağımsız canlı mini alan grafikleriyle gösteren bileşen.
 */
class CoreGridWidget : public QScrollArea {
    Q_OBJECT

public:
    explicit CoreGridWidget(QWidget* parent = nullptr);

    void updateCores(const QVector<CoreTelemetry>& cores);
    void applyVisualSettings();

private:
    struct CoreTile {
        QLabel* nameLabel{nullptr};
        QLabel* statLabel{nullptr};
        AreaChartWidget* chart{nullptr};
    };

    void rebuildGrid(int coreCount);

    QWidget* m_container{nullptr};
    QGridLayout* m_grid{nullptr};
    QVector<CoreTile> m_tiles;
};

} // namespace sysmon
