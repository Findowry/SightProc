#include "core/Lang.h"

#include <atomic>

namespace sysmon {

namespace {
std::atomic<int> g_lang{static_cast<int>(Language::English)};
} // namespace

Language currentLanguage() noexcept {
    return static_cast<Language>(g_lang.load(std::memory_order_relaxed));
}

void setCurrentLanguage(Language lang) noexcept {
    g_lang.store(static_cast<int>(lang), std::memory_order_relaxed);
}

QString processStateText(char stateChar) {
    switch (stateChar) {
    case 'R':
        return trText("Running (R)", "Çalışıyor (R)");
    case 'S':
        return trText("Sleeping (S)", "Uyuyor (S)");
    case 'D':
        return trText("Disk Sleep (D)", "Disk Uykusu (D)");
    case 'T':
    case 't':
        return trText("Stopped (%1)", "Durduruldu (%1)").arg(QLatin1Char(stateChar));
    case 'Z':
        return trText("Zombie (Z)", "Zombi (Z)");
    case 'I':
        return trText("Idle (I)", "Boşta (I)");
    default:
        return trText("Other (%1)", "Diğer (%1)").arg(QLatin1Char(stateChar));
    }
}

} // namespace sysmon
