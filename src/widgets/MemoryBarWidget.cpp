#include "widgets/MemoryBarWidget.h"

#include "core/AppSettings.h"
#include "core/FormatUtils.h"
#include "core/Lang.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace sysmon {

MemoryBarWidget::MemoryBarWidget(QWidget* parent)
    : QWidget(parent) {
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MemoryBarWidget::setMemory(const MemoryTelemetry& mem) {
    m_memory = mem;
    update();
}

QSize MemoryBarWidget::sizeHint() const {
    return QSize(360, 48);
}

QSize MemoryBarWidget::minimumSizeHint() const {
    return QSize(180, 46);
}

void MemoryBarWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = QApplication::palette();
    const double total = static_cast<double>(std::max<std::uint64_t>(m_memory.totalBytes, 1));
    const double usedFrac = std::clamp(static_cast<double>(m_memory.usedBytes) / total, 0.0, 1.0);
    const std::uint64_t bufCache = m_memory.buffersBytes + m_memory.cachedBytes;
    const double cacheFrac = std::clamp(
        static_cast<double>(bufCache) / total,
        0.0,
        std::max(0.0, 1.0 - usedFrac));

    const QRectF barRect(4.0, 4.0, std::max(10.0, static_cast<double>(width()) - 8.0), 14.0);

    QColor trackCol = pal.color(QPalette::Mid);
    trackCol.setAlphaF(0.22f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackCol);
    painter.drawRoundedRect(barRect, 4.0, 4.0);

    QColor usedCol = AppSettings::instance().categoryColor(AppSettings::HardwareCategory::Memory);
    QColor cacheCol = pal.color(QPalette::Link);
    {
        float uh = 0.0f;
        float us = 0.0f;
        float ul = 0.0f;
        float ch = 0.0f;
        float cs = 0.0f;
        float cl = 0.0f;
        usedCol.getHslF(&uh, &us, &ul);
        cacheCol.getHslF(&ch, &cs, &cl);
        const float diff = std::fabs(uh - ch);
        if (std::min(diff, 1.0f - diff) < 0.12f) {
            cacheCol = QColor::fromHslF(std::fmod(uh + 0.35f, 1.0f),
                                        std::clamp(us, 0.35f, 0.85f),
                                        std::clamp(ul, 0.35f, 0.65f));
        }
    }
    cacheCol.setAlphaF(0.70f);

    painter.save();
    painter.setClipRect(barRect);
    const double usedW = barRect.width() * usedFrac;
    const double cacheW = barRect.width() * cacheFrac;
    if (usedW > 0.5) {
        painter.setBrush(usedCol);
        painter.drawRoundedRect(QRectF(barRect.left(), barRect.top(), usedW, barRect.height()),
                                4.0,
                                4.0);
    }
    if (cacheW > 0.5) {
        painter.setBrush(cacheCol);
        painter.drawRect(QRectF(barRect.left() + usedW, barRect.top(), cacheW, barRect.height()));
    }
    painter.restore();

    const QFontMetrics fm(font());
    const double legendY = barRect.bottom() + 6.0;
    const double legendH = static_cast<double>(fm.height());

    struct LegendEntry {
        QColor color;
        QString text;
    };
    const LegendEntry entries[] = {
        {usedCol, QStringLiteral("%1: %2").arg(trText("Used", "Kullanılan"),
                                               fmt::formatBytes(m_memory.usedBytes))},
        {cacheCol, QStringLiteral("%1: %2").arg(trText("Cache/Buf", "Önbellek/Tampon"),
                                                fmt::formatBytes(bufCache))},
        {trackCol, QStringLiteral("%1: %2").arg(trText("Available", "Kullanılabilir"),
                                                fmt::formatBytes(m_memory.availableBytes))},
    };

    double x = barRect.left();
    for (const auto& entry : entries) {
        const QRectF swatch(x, legendY + (legendH - 10.0) * 0.5, 10.0, 10.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(entry.color);
        painter.drawRoundedRect(swatch, 2.0, 2.0);
        x += 15.0;

        const int tw = fm.horizontalAdvance(entry.text);
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(QRectF(x, legendY, static_cast<double>(tw) + 4.0, legendH),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         entry.text);
        x += static_cast<double>(tw) + 18.0;
    }
}

void MemoryBarWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        update();
    }
    QWidget::changeEvent(event);
}

} // namespace sysmon
