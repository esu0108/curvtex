#pragma once
#include <Windows.h>

#include <string>

// ファイルダイアログ。パスは UTF-8 で受け渡す。キャンセル時は空文字列を返す。
std::string OpenModelDialog(HWND owner);
std::string SaveImageDialog(HWND owner, const std::string& initialPath);
