#pragma once

#include <QString>

namespace sysmon {

enum class Language {
    English = 0,
    Turkish = 1
};

[[nodiscard]] Language currentLanguage() noexcept;
void setCurrentLanguage(Language lang) noexcept;

[[nodiscard]] inline QString trText(const char* en, const char* tr) {
    return QString::fromUtf8(currentLanguage() == Language::Turkish ? tr : en);
}

[[nodiscard]] QString processStateText(char stateChar);

} // namespace sysmon
