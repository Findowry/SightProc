#pragma once

#include <QObject>
#include <QStyledItemDelegate>

class QAbstractScrollArea;
class QEvent;

namespace sysmon {

/**
 * @brief Kvantum cam/bulanıklık yüzeyinin tablo ve listelerin arkasından
 *        sızabilmesi için QPalette::Base ve AlternateBase alfa değerlerini ayarlar.
 */
void applyTranslucentViewPalette(QAbstractScrollArea* view, int opacityPercent);

/**
 * @brief Yerel QStyle çizimini bozmadan yalnızca satır yüksekliğini ölçekleyen delege.
 */
class ScaledRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ScaledRowDelegate(int baseRowHeight = 28, QObject* parent = nullptr);

    void setScalePercent(int percent);
    void setBaseRowHeight(int basePx);

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

private:
    int m_baseHeight{28};
    int m_scalePercent{100};
};

/**
 * @brief Ctrl + Fare Tekerleği ile her sekmenin kendi ölçeğini bağımsız ayarlayan olay filtresi.
 */
class ZoomWheelFilter : public QObject {
    Q_OBJECT

public:
    explicit ZoomWheelFilter(int tabIndex, QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int m_tabIndex{0};
};

} // namespace sysmon
