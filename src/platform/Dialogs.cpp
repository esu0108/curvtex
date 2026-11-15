#include "platform/Dialogs.h"

#include <commdlg.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>

#include "image/ImageWriter.h"
#include "platform/PathUtil.h"

namespace fs = std::filesystem;

namespace {
constexpr size_t kPathBufferChars = 32768; // 長いパスにも対応(Windows のパス長の上限)
}

std::string OpenModelDialog(HWND owner) {
    std::wstring  buf(kPathBufferChars, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"3Dモデル (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF (*.gltf;*.glb)\0*.gltf;*.glb\0FBX (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0\0";
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

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0Bitmap (*.bmp)\0*.bmp\0TGA (*.tga)\0*.tga\0\0";
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
