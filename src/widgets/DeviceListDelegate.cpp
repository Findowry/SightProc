#include "widgets/DeviceListDelegate.h"

#include "core/AppSettings.h"
#include "models/DeviceListModel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>

#include <algorithm>
#include <vector>

namespace sysmon {

namespace {

void paintMiniSparkline(QPainter* painter,
                        const QRectF& rect,
                        const DeviceEntry& entry,
                        const QPalette& pal,
                        const QColor& catColor,
                        bool selected) {
    QColor borderCol = selected
        ? pal.color(QPalette::HighlightedText)
        : catColor;
    borderCol.setAlphaF(0.45f);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(borderCol, 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect, 3.0, 3.0);

    const std::size_t count = entry.primaryHistory.size();
    if (count == 0) {
        painter->restore();
        return;
    }

    const QRectF inner = rect.adjusted(2.0, 2.0, -2.0, -2.0);
    double yMax = entry.fixedMax;
    if (entry.autoScale) {
        const double peak = std::max(entry.primaryHistory.maxValue(0.0),
                                     entry.secondaryHistory.maxValue(0.0));
        yMax = std::max(entry.fixedMax, peak * 1.15);
    }
    yMax = std::max(yMax, 1e-6);

    const std::size_t capacity = entry.primaryHistory.capacity();
    const double stepX = (capacity > 1)
        ? (inner.width() / static_cast<double>(capacity - 1))
        : inner.width();

    std::vector<QPointF> pts;
    pts.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double norm = std::clamp(entry.primaryHistory[i] / yMax, 0.0, 1.0);
        const double x = inner.right() - static_cast<double>(count - 1 - i) * stepX;
        const double y = inner.bottom() - norm * inner.height();
        pts.emplace_back(x, y);
    }

    const QColor accent = selected
        ? pal.color(QPalette::HighlightedText)
        : catColor;

    painter->setClipRect(inner.adjusted(-0.5, -1.0, 0.5, 1.0));

    if (pts.size() == 1) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent);
        painter->drawEllipse(pts.front(), 2.0, 2.0);
        painter->restore();
        return;
    }

    QPainterPath stroke;
    stroke.moveTo(pts.front());
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const double midX = (pts[i - 1].x() + pts[i].x()) * 0.5;
        stroke.cubicTo(QPointF(midX, pts[i - 1].y()), QPointF(midX, pts[i].y()), pts[i]);
    }

    QPainterPath fill = stroke;
    fill.lineTo(pts.back().x(), inner.bottom());
    fill.lineTo(pts.front().x(), inner.bottom());
    fill.closeSubpath();

    QColor topCol = accent;
    topCol.setAlphaF(0.38f);
    QColor botCol = accent;
    botCol.setAlphaF(0.04f);
    QLinearGradient grad(0.0, inner.top(), 0.0, inner.bottom());
    grad.setColorAt(0.0, topCol);
    grad.setColorAt(1.0, botCol);

    painter->setPen(Qt::NoPen);
    painter->setBrush(grad);
    painter->drawPath(fill);

    painter->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(stroke);
    painter->restore();
}

} // namespace

DeviceListDelegate::DeviceListDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void DeviceListDelegate::setScalePercent(int percent) {
    m_scalePercent = std::clamp(percent, 70, 200);
}

void DeviceListDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    opt.text.clear();
    opt.icon = QIcon();
    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

    const auto* model = qobject_cast<const DeviceListModel*>(index.model());
    const DeviceEntry* entry = model ? model->entryAt(index.row()) : nullptr;
    if (!entry) {
        return;
    }

    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const QPalette& pal = opt.palette;
    const QColor catColor = AppSettings::instance().categoryColor(entry->category);

    const double scale = static_cast<double>(m_scalePercent) / 100.0;
    const QRect r = opt.rect.adjusted(10, 6, -8, -6);

    // Sol kenarda kategori renk vurgusu çubuğu
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(catColor);
    painter->drawRoundedRect(QRectF(opt.rect.left() + 3, opt.rect.top() + 7, 3.0,
                                    std::max(10, opt.rect.height() - 14)),
                             1.5, 1.5);
    painter->restore();

    const double sparkW = std::clamp(58.0 * scale, 48.0, 96.0);
    const double sparkH = std::clamp(36.0 * scale, 28.0, static_cast<double>(r.height()));
    const QRectF sparkRect(r.left() + 2, r.top() + (r.height() - sparkH) * 0.5, sparkW, sparkH);
    paintMiniSparkline(painter, sparkRect, *entry, pal, catColor, selected);

    const int textLeft = static_cast<int>(sparkRect.right()) + 10;
    const int textWidth = std::max(10, r.right() - textLeft);

    QFont titleFont = opt.font;
    titleFont.setBold(true);
    const QFontMetrics titleFm(titleFont);

    QFont subFont = opt.font;
    subFont.setPointSizeF(std::max(7.5, subFont.pointSizeF() - 1.0));
    const QFontMetrics subFm(subFont);

    const int totalTextH = titleFm.height() + subFm.height() + 2;
    const int textTop = r.top() + std::max(0, (r.height() - totalTextH) / 2);

    const QColor titleCol = selected
        ? pal.color(QPalette::HighlightedText)
        : pal.color(QPalette::Text);
    QColor subCol = selected
        ? pal.color(QPalette::HighlightedText)
        : pal.color(QPalette::PlaceholderText);
    if (selected) {
        subCol.setAlphaF(0.85f);
    }

    painter->save();
    painter->setFont(titleFont);
    painter->setPen(titleCol);
    const QString elidedTitle = titleFm.elidedText(entry->title, Qt::ElideRight, textWidth);
    painter->drawText(QRect(textLeft, textTop, textWidth, titleFm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elidedTitle);

    painter->setFont(subFont);
    painter->setPen(subCol);
    const QString elidedSub = subFm.elidedText(entry->subtitle, Qt::ElideRight, textWidth);
    painter->drawText(QRect(textLeft, textTop + titleFm.height() + 2, textWidth, subFm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elidedSub);
    painter->restore();
}

QSize DeviceListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex&) const {
    const QFontMetrics fm(option.font);
    const int base = std::max(54, fm.height() * 2 + 18);
    const int scaled = (base * m_scalePercent) / 100;
    return QSize(static_cast<int>(230 * m_scalePercent / 100), std::max(base, scaled));
}

} // namespace sysmon
