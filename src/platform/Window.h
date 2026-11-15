#pragma once
#include <Windows.h>

#include <functional>
#include <string>

// Win32 のウィンドウ。メッセージ処理・DPI・ドラッグ&ドロップをまとめ、アプリへはコールバックで通知する。
class Window {
public:
    struct Callbacks {
        std::function<void(UINT width, UINT height)> onResize;      // クライアント領域のサイズ変更(最小化は通知しない)
        std::function<void(float scale)>             onDpiChanged;  // DPI 倍率(96DPI=1.0)の変更
        std::function<void(std::wstring path)>       onDropFile;    // ファイルのドロップ(複数なら先頭の1つ)
    };

    ~Window();

    // logicalWidth/Height は 96DPI 基準のサイズ。ウィンドウがあるモニタの DPI に合わせて拡大して作る。
    void Create(HINSTANCE hInst, const wchar_t* title, int logicalWidth, int logicalHeight, Callbacks callbacks);
    void Show(int nShow);
    void Destroy();

    // 溜まっているメッセージを処理する。WM_QUIT を受け取ったら false。
    bool PumpMessages();

    HWND Handle() const { return hwnd_; }
    bool IsMinimized() const { return IsIconic(hwnd_) != FALSE; }

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT                 OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND         hwnd_ = nullptr;
    HINSTANCE    hInst_ = nullptr;
    std::wstring className_;
    Callbacks    cb_;
};
