#pragma once

#include "core/TelemetryTypes.h"

#include <QWidget>

namespace sysmon {

/**
 * @brief Bellek dağılımını (Kullanılan / Önbellek-Tampon / Kullanılabilir)
 *        sistem paletiyle görselleştiren parçalı çubuk bileşeni.
 */
class MemoryBarWidget : public QWidget {
    Q_OBJECT

public:
    explicit MemoryBarWidget(QWidget* parent = nullptr);

    void setMemory(const MemoryTelemetry& mem);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    MemoryTelemetry m_memory;
};

} // namespace sysmon
