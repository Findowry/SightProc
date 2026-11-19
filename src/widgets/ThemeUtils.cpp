#include "widgets/ThemeUtils.h"

#include "core/AppSettings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QPalette>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>

namespace sysmon {

void applyTranslucentViewPalette(QAbstractScrollArea* view, int opacityPercent) {
    if (!view || !view->viewport()) {
        return;
    }

    const int clamped = std::clamp(opacityPercent, 0, 100);
    const float alpha = static_cast<float>(clamped) / 100.0f;
    const float altAlpha = (clamped == 0)
        ? 0.06f
        : std::clamp(alpha * 0.72f + 0.05f, 0.0f, 1.0f);

    QPalette pal = QApplication::palette();
    QColor base = pal.color(QPalette::Base);
    base.setAlphaF(alpha);
    QColor alt = pal.color(QPalette::AlternateBase);
    alt.setAlphaF(altAlpha);

    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, alt);

    view->setPalette(pal);
    view->viewport()->setPalette(pal);
    view->viewport()->setAutoFillBackground(clamped > 0);
    view->viewport()->update();
}

ScaledRowDelegate::ScaledRowDelegate(int baseRowHeight, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_baseHeight(std::max(18, baseRowHeight)) {}

void ScaledRowDelegate::setScalePercent(int percent) {
    m_scalePercent = std::clamp(percent, 70, 200);
}

void ScaledRowDelegate::setBaseRowHeight(int basePx) {
    m_baseHeight = std::max(18, basePx);
}

QSize ScaledRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    QSize sz = QStyledItemDelegate::sizeHint(option, index);
    const QFontMetrics fm(option.font);
    const int minFromFont = fm.height() + 10;
    const int scaledBase = (m_baseHeight * m_scalePercent) / 100;
    sz.setHeight(std::max({sz.height(), minFromFont, scaledBase}));
    return sz;
}

ZoomWheelFilter::ZoomWheelFilter(int tabIndex, QObject* parent)
    : QObject(parent)
    , m_tabIndex(tabIndex) {}

bool ZoomWheelFilter::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers().testFlag(Qt::ControlModifier)) {
            const int dy = we->angleDelta().y();
            if (dy != 0) {
                auto& settings = AppSettings::instance();
                const int step = (dy > 0) ? 10 : -10;
                settings.setTabScalePercent(m_tabIndex,
                                            settings.tabScalePercent(m_tabIndex) + step);
                we->accept();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace sysmon
