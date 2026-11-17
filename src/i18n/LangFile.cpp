#include "i18n/LangFile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool ReadHex4(std::string_view s, size_t pos, unsigned& v) {
    if (pos + 4 > s.size()) return false;
    v = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = s[pos + i];
        unsigned   d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        v = v * 16 + d;
    }
    return true;
}

// s は '"' の次の文字から。閉じる '"' の位置を end に返す。エスケープを解いた結果を out へ。失敗すると理由を返す (成功なら nullptr)。
const char* ParseQuoted(std::string_view s, std::string& out, size_t& end) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            end = i;
            return nullptr;
        }
        if (c != '\\') {
            out += c;
            ++i;
            continue;
        }
        if (i + 1 >= s.size()) return "dangling backslash";
        const char n = s[i + 1];
        i += 2;
        switch (n) {
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'u': {
            unsigned cp;
            if (!ReadHex4(s, i, cp)) return "bad \\u escape";
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00) { // 上位サロゲート: 続く \uXXXX (下位サロゲート) と対にする
                unsigned lo;
                if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u' && ReadHex4(s, i + 2, lo) && lo >= 0xDC00 && lo < 0xE000) {
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    return "unpaired surrogate in \\u escape";
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                return "unpaired surrogate in \\u escape";
            }
            AppendUtf8(out, cp);
            break;
        }
        default: return "unknown escape sequence";
        }
    }
    return "missing closing quote";
}

bool ValidKey(std::string_view k) {
    if (k.empty()) return false;
    size_t i = 0;
    if (k[0] == '@') i = 1;
    if (i >= k.size()) return false;
    for (; i < k.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(k[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

} // namespace

void ParseLangText(std::string_view text, const std::string& label, LangFileData& out, std::vector<std::string>& warnings) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);

    int         lineNo = 0;
    std::string value;
    while (!text.empty()) {
        ++lineNo;
        const size_t nl   = text.find('\n');
        std::string_view line = Trim(text.substr(0, nl));
        text                  = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty() || line.front() == '#') continue;

        auto warn = [&](const std::string& why) { warnings.push_back(label + ":" + std::to_string(lineNo) + ": " + why); };

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn("expected Key = \"text\"");
            continue;
        }
        const std::string_view key  = Trim(line.substr(0, eq));
        std::string_view       rest = Trim(line.substr(eq + 1));
        if (!ValidKey(key)) {
            warn("invalid key");
            continue;
        }
        if (key.front() == '@' && !rest.empty() && rest.front() != '"') {
            // ヘッダーは、引用符なしの値も許す (@order = 10)。# から後ろはコメント。
            const size_t hash = rest.find('#');
            value             = std::string(Trim(rest.substr(0, hash)));
        } else {
            if (rest.empty() || rest.front() != '"') {
                warn("the value must be in double quotes");
                continue;
            }
            size_t      end = 0;
            const char* why = ParseQuoted(rest.substr(1), value, end);
            if (why) {
                warn(why);
                continue;
            }
            const std::string_view after = Trim(rest.substr(1 + end + 1));
            if (!after.empty() && after.front() != '#') {
                warn("unexpected text after the closing quote");
                continue;
            }
        }

        if (key.front() == '@') {
            const std::string k = Lower(std::string(key));
            if (k == "@name") {
                out.name = value;
            } else if (k == "@match") {
                out.match.clear();
                size_t p = 0;
                while (p < value.size()) {
                    while (p < value.size() && (value[p] == ' ' || value[p] == '\t')) ++p;
                    size_t q = p;
                    while (q < value.size() && value[q] != ' ' && value[q] != '\t') ++q;
                    if (q > p) out.match.push_back(Lower(value.substr(p, q - p)));
                    p = q;
                }
            } else if (k == "@font") {
                out.font = Lower(value);
            } else if (k == "@order") {
                out.order = std::atoi(value.c_str());
            } else {
                warn("unknown header " + std::string(key));
            }
            continue;
        }
        out.entries.emplace_back(std::string(key), value);
    }
}

std::vector<std::string> PrintfSpecs(std::string_view s) {
    std::vector<std::string> specs;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '%') {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < s.size() && std::string_view("-+ #0123456789.*hlzjtLqI").find(s[j]) != std::string_view::npos) ++j;
        if (j < s.size()) ++j; // 変換文字 (u, s, g, X など)
        specs.emplace_back(s.substr(i, j - i));
        i = j - 1;
    }
    return specs;
}

int MatchLanguage(std::string tag, const std::vector<std::vector<std::string>>& matches) {
    tag = Lower(std::move(tag));
    auto primary = [](const std::string& t) { return t.substr(0, t.find('-')); };
    const std::string prim = primary(tag);
    for (size_t i = 0; i < matches.size(); ++i)
        for (const auto& m : matches[i])
            if (m == tag) return static_cast<int>(i);
    for (size_t i = 0; i < matches.size(); ++i)
        for (const auto& m : matches[i])
            if (m == prim) return static_cast<int>(i);
    for (size_t i = 0; i < matches.size(); ++i)
        for (const auto& m : matches[i])
            if (primary(m) == prim) return static_cast<int>(i);
    return -1;
}
