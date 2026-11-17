#pragma once
#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

// 実行ファイルの隣の assets フォルダ。コンパイル済みシェーダー (assets/shaders/*.cso) と
// 言語ファイル (assets/lang/*.lang) を置く。CMake がビルド後に、実行ファイルの隣へコピーする。
inline std::filesystem::path ExeDir() {
    std::vector<wchar_t> buf(32768);
    const DWORD          n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return std::filesystem::path(L".");
    return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
}

// assets フォルダの中のパス。rel は "shaders/mip_vs.cso" のような ASCII の相対パス。
inline std::filesystem::path AssetPath(const char* rel) {
    return ExeDir() / L"assets" / std::filesystem::path(rel);
}
