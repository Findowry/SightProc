#pragma once

#include <QStyledItemDelegate>

namespace sysmon {

/**
 * @brief Sol cihaz listesinde Kvantum'un PE_PanelItemViewItem çizimini koruyarak
 *        kategori rengiyle canlı mini sparkline alanı ve metin çizen öğe delegesi.
 */
class DeviceListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit DeviceListDelegate(QObject* parent = nullptr);

    void setScalePercent(int percent);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

private:
    int m_scalePercent{115};
};

} // namespace sysmon
