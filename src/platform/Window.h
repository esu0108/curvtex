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
        // 枠をドラッグしてサイズを変えている最中に、サイズが変わるたび(onResize の直後)に呼ぶ。
        // サイズ変更中は OS のループがメッセージを処理し続け、アプリのメインループが回らないので、ここで1フレーム描く。
        std::function<void()>                        onSizingFrame;
    };

    ~Window();

    // logicalWidth/Height は 96DPI 基準のサイズ。ウィンドウがあるモニタの DPI に合わせて拡大して作る。
    void Create(HINSTANCE hInst, const wchar_t* title, int logicalWidth, int logicalHeight, Callbacks callbacks);
    void Show(int nShow);
    void Destroy();

    // 溜まっているメッセージを処理する。WM_QUIT を受け取ったら false。
    // 1件でも処理したら hadMessage が true になる (入力があったので再描画が必要、の合図に使う)。
    bool PumpMessages(bool& hadMessage);

    // 新しいメッセージが来るか timeoutMs が経つまで眠る (INFINITE で無期限)。メッセージが来て起きたら true、タイムアウトなら false。
    bool WaitForMessage(DWORD timeoutMs);

    HWND Handle() const { return hwnd_; }
    bool IsMinimized() const { return IsIconic(hwnd_) != FALSE; }

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT                 OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND         hwnd_ = nullptr;
    HINSTANCE    hInst_ = nullptr;
    std::wstring className_;
    Callbacks    cb_;
    bool         sizing_ = false; // 枠のドラッグによるサイズ変更 (移動も含む) の最中
};
