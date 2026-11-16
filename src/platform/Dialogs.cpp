#include "platform/Dialogs.h"

#include <commdlg.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <initializer_list>
#include <utility>

#include "i18n/Strings.h"
#include "image/ImageWriter.h"
#include "platform/PathUtil.h"

namespace fs = std::filesystem;

namespace {
constexpr size_t kPathBufferChars = 32768; // 長いパスにも対応(Windows のパス長の上限)

std::wstring ToWide(const char* utf8) { return PathFromUtf8(utf8).wstring(); }

// 「表示名\0パターン\0 ... \0\0」形式のフィルタ文字列を作る。表示名は現在の言語に合わせて呼び出し側が渡す。
std::wstring MakeFilter(std::initializer_list<std::pair<std::wstring, const wchar_t*>> items) {
    std::wstring f;
    for (const auto& [name, pattern] : items) {
        f += name;
        f.push_back(L'\0');
        f += pattern;
        f.push_back(L'\0');
    }
    f.push_back(L'\0');
    return f;
}
} // namespace

std::string OpenModelDialog(HWND owner) {
    std::wstring  buf(kPathBufferChars, L'\0');
    const std::wstring title  = ToWide(Tr(Str::DlgOpenTitle));
    const std::wstring filter = MakeFilter({
        {ToWide(Tr(Str::DlgFilter3D)), L"*.obj;*.gltf;*.glb;*.fbx"},
        {L"Wavefront OBJ (*.obj)", L"*.obj"},
        {L"glTF (*.gltf;*.glb)", L"*.gltf;*.glb"},
        {L"FBX (*.fbx)", L"*.fbx"},
        {ToWide(Tr(Str::DlgFilterAll)), L"*.*"},
    });

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrTitle  = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = static_cast<DWORD>(buf.size());
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return PathToUtf8(fs::path(buf.c_str()));
}

std::string SaveImageDialog(HWND owner, const std::string& initialPath) {
    std::wstring buf(kPathBufferChars, L'\0');
    const fs::path initial = PathFromUtf8(initialPath);
    wcsncpy_s(buf.data(), buf.size(), initial.c_str(), _TRUNCATE);

    const std::wstring title  = ToWide(Tr(Str::DlgSaveTitle));
    const std::wstring filter = MakeFilter({
        {L"PNG (*.png)", L"*.png"},
        {L"Bitmap (*.bmp)", L"*.bmp"},
        {L"TGA (*.tga)", L"*.tga"},
    });

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrTitle  = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    // 現在のパスの拡張子に合わせてフィルタを選んでおく (1=PNG, 2=BMP, 3=TGA)
    switch (ImageFormatFromPath(initial)) {
    case ImageFormat::Bmp: ofn.nFilterIndex = 2; break;
    case ImageFormat::Tga: ofn.nFilterIndex = 3; break;
    default: ofn.nFilterIndex = 1; break;
    }
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile  = static_cast<DWORD>(buf.size());
    ofn.Flags     = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};

    fs::path chosen(buf.c_str());
    if (!chosen.has_extension()) { // 拡張子なしで確定した場合は、選択中のフィルタの形式にする
        static const wchar_t* exts[] = {L".png", L".bmp", L".tga"};
        chosen += exts[std::clamp<DWORD>(ofn.nFilterIndex, 1, 3) - 1];
    }
    return PathToUtf8(chosen);
}
