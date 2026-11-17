#include "i18n/Strings.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <unordered_map>

#include "i18n/LangFile.h"
#include "platform/AssetPath.h"
#include "platform/PathUtil.h"

namespace fs = std::filesystem;

namespace {

// 組み込みの英語 (どの言語ファイルにも無い文字列の既定値であり、キーの一覧でもある)
#define I18N_DECLARE_EN(id, en) en,
constexpr const char* kEnglish[] = {I18N_STRINGS(I18N_DECLARE_EN)};
#undef I18N_DECLARE_EN
#define I18N_DECLARE_KEY(id, en) {#id, Str::id},
const std::unordered_map<std::string_view, Str> kKeys = {I18N_STRINGS(I18N_DECLARE_KEY)};
#undef I18N_DECLARE_KEY
static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(Str::Count), "string table size mismatch");

struct Language {
    std::string              code; // ファイル名 (拡張子なし)
    std::string              name;
    std::vector<std::string> match;
    FontKind                 font  = FontKind::Ja;
    int                      order = 1000;
    std::vector<std::string> text; // Str ごと。has が 0 なら英語を使う
    std::vector<uint8_t>     has;
};

struct LangSet {
    std::vector<Language>    langs;
    std::vector<std::string> diag;
};

FontKind FontKindFromString(const std::string& s, bool& known) {
    known = true;
    if (s.empty() || s == "ja") return FontKind::Ja;
    if (s == "zh") return FontKind::Zh;
    if (s == "zh-hant") return FontKind::ZhHant;
    if (s == "ko") return FontKind::Ko;
    if (s == "th") return FontKind::Th;
    if (s == "latin") return FontKind::Latin;
    known = false;
    return FontKind::Ja;
}

fs::path LangDir() { return AssetPath("lang"); }

void LoadOne(const fs::path& path, LangSet& set) {
    const std::string code  = PathToUtf8(path.stem());
    const std::string label = PathToUtf8(path.filename());

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        set.diag.push_back(label + ": cannot open the file");
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    LangFileData data;
    ParseLangText(text, label, data, set.diag);

    Language lang;
    lang.code  = code;
    lang.name  = data.name.empty() ? code : data.name;
    lang.match = data.match;
    lang.order = data.order;
    bool known = true;
    lang.font  = FontKindFromString(data.font, known);
    if (!known) set.diag.push_back(label + ": unknown @font \"" + data.font + "\" (use ja, zh, zh-hant, ko, th or latin)");
    lang.text.assign(static_cast<size_t>(Str::Count), std::string());
    lang.has.assign(static_cast<size_t>(Str::Count), 0);

    for (auto& [key, value] : data.entries) {
        const auto it = kKeys.find(key);
        if (it == kKeys.end()) {
            set.diag.push_back(label + ": unknown key " + key);
            continue;
        }
        const size_t i = static_cast<size_t>(it->second);
        // printf の指定子が英語と違う訳は使わない (書式の食い違いは、実行時に不正なメモリアクセスになる)
        if (PrintfSpecs(value) != PrintfSpecs(kEnglish[i])) {
            set.diag.push_back(label + ": " + key + " has different printf specifiers from the English text (ignored)");
            continue;
        }
        lang.text[i] = std::move(value);
        lang.has[i]  = 1;
    }
    set.langs.push_back(std::move(lang));
}

LangSet LoadAll() {
    LangSet set;

    const fs::path dir = LangDir();
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (fs::directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec)) {
            std::error_code ec2;
            if (it->is_regular_file(ec2) && it->path().extension() == L".lang") LoadOne(it->path(), set);
        }
    } else {
        set.diag.push_back("Language folder not found: " + PathToUtf8(dir));
    }

    // 英語は組み込みなので、en.lang が無くても選べるようにする
    const bool hasEn = std::any_of(set.langs.begin(), set.langs.end(), [](const Language& l) { return l.code == "en"; });
    if (!hasEn) {
        Language en;
        en.code  = "en";
        en.name  = "English";
        en.match = {"en"};
        en.order = 20;
        en.text.assign(static_cast<size_t>(Str::Count), std::string());
        en.has.assign(static_cast<size_t>(Str::Count), 0);
        set.langs.push_back(std::move(en));
    }

    std::stable_sort(set.langs.begin(), set.langs.end(), [](const Language& a, const Language& b) { return a.order != b.order ? a.order < b.order : a.code < b.code; });
    for (const std::string& d : set.diag) OutputDebugStringA(("curvtex lang: " + d + "\n").c_str());
    return set;
}

// 起動後は変えない (Tr がワーカースレッドからも呼ばれるので、読み込み後は読み取り専用にしておく)
const LangSet& Langs() {
    static const LangSet set = LoadAll();
    return set;
}

int EnglishIndex() {
    const auto& l = Langs().langs;
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].code == "en") return static_cast<int>(i);
    return 0;
}

int DetectSystemLang() {
    const auto& langs = Langs().langs;
    wchar_t     name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        std::string tag;
        for (const wchar_t* p = name; *p; ++p) tag += static_cast<char>(*p < 128 ? *p : '?'); // ロケール名は ASCII
        std::vector<std::vector<std::string>> matches;
        for (const Language& l : langs) matches.push_back(l.match);
        const int i = MatchLanguage(tag, matches);
        if (i >= 0) return i;
    }
    return EnglishIndex();
}

std::atomic<int>& CurrentLang() {
    static std::atomic<int> lang{DetectSystemLang()};
    return lang;
}

} // namespace

int LangCount() { return static_cast<int>(Langs().langs.size()); }

int  GetLang() { return CurrentLang().load(std::memory_order_relaxed); }
void SetLang(int lang) {
    if (lang >= 0 && lang < LangCount()) CurrentLang().store(lang, std::memory_order_relaxed);
}

const char* LangNativeName(int lang) {
    const auto& l = Langs().langs;
    return (lang >= 0 && lang < static_cast<int>(l.size())) ? l[static_cast<size_t>(lang)].name.c_str() : "";
}

FontKind LangFontKind(int lang) {
    const auto& l = Langs().langs;
    return (lang >= 0 && lang < static_cast<int>(l.size())) ? l[static_cast<size_t>(lang)].font : FontKind::Ja;
}

const std::vector<std::string>& LangDiagnostics() { return Langs().diag; }

const char* Tr(int lang, Str id) {
    const size_t i = static_cast<size_t>(id);
    if (i >= static_cast<size_t>(Str::Count)) return "";
    const auto& l = Langs().langs;
    if (lang >= 0 && lang < static_cast<int>(l.size()) && l[static_cast<size_t>(lang)].has[i]) return l[static_cast<size_t>(lang)].text[i].c_str();
    return kEnglish[i];
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
