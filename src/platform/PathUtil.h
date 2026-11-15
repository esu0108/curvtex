#pragma once
#include <filesystem>
#include <string>

// アプリ内のパス文字列は UTF-8 の std::string で持ち、ファイルを開く所でだけ fs::path へ変換する。
// (固定長バッファや Win32 の MultiByteToWideChar に頼らないので、長さの制限もコードページの問題も無い)
inline std::filesystem::path PathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline std::string PathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// 前後の空白と、エクスプローラの「パスのコピー」で付く引用符を除去
inline std::string CleanPath(std::string p) {
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t' || p.back() == '"' || p.back() == '\r' || p.back() == '\n')) p.pop_back();
    size_t b = 0;
    while (b < p.size() && (p[b] == ' ' || p[b] == '\t' || p[b] == '"')) ++b;
    return p.substr(b);
}
