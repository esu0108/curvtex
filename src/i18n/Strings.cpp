#include "i18n/Strings.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>

namespace {

// 言語ごとの文字列 (Lang の並びと同じ)
struct Entry {
    const char* t[kLangCount];
};

#define I18N_DECLARE_ENTRY(id, ja, en, zh, ko, es, fr, de, ru) {{ja, en, zh, ko, es, fr, de, ru}},
constexpr Entry kTable[] = {I18N_STRINGS(I18N_DECLARE_ENTRY)};
#undef I18N_DECLARE_ENTRY
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Str::Count), "string table size mismatch");

constexpr const char* kLangNames[kLangCount] = {"日本語", "English", "简体中文", "한국어", "Español", "Français", "Deutsch", "Русский"};

Lang DetectSystemLang() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_JAPANESE: return Lang::Ja;
    case LANG_CHINESE: return Lang::Zh;
    case LANG_KOREAN: return Lang::Ko;
    case LANG_SPANISH: return Lang::Es;
    case LANG_FRENCH: return Lang::Fr;
    case LANG_GERMAN: return Lang::De;
    case LANG_RUSSIAN: return Lang::Ru;
    default: return Lang::En;
    }
}

std::atomic<Lang>& CurrentLang() {
    static std::atomic<Lang> lang{DetectSystemLang()};
    return lang;
}

} // namespace

Lang GetLang() { return CurrentLang().load(std::memory_order_relaxed); }
void SetLang(Lang lang) { CurrentLang().store(lang, std::memory_order_relaxed); }

const char* LangNativeName(Lang lang) {
    const int i = static_cast<int>(lang);
    return (i >= 0 && i < kLangCount) ? kLangNames[i] : "";
}

const char* Tr(Lang lang, Str id) {
    const size_t i = static_cast<size_t>(id);
    const int    l = static_cast<int>(lang);
    if (i >= static_cast<size_t>(Str::Count) || l < 0 || l >= kLangCount) return "";
    return kTable[i].t[l];
}

const char* Tr(Str id) { return Tr(GetLang(), id); }

std::string FormatStatus(const StatusMsg& m) {
    if (m.id == Str::StLoaded) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), Tr(m.id), m.verts, m.tris, m.radius);
        return buf;
    }
    return std::string(Tr(m.id)) + m.detail;
}
