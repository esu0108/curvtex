#include "platform/Window.h"

#include <shellapi.h>

#include <stdexcept>
#include <utility>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "platform/resource.h"

// imgui_impl_win32.h では #if 0 で隠されているので自前で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Window::~Window() {
    Destroy();
}

void Window::Create(HINSTANCE hInst, const wchar_t* title, int logicalWidth, int logicalHeight, Callbacks callbacks) {
    hInst_ = hInst;
    cb_    = std::move(callbacks);

    // 通常は curvtex.manifest(PerMonitorV2) で宣言済み。マニフェストが無い場合のフォールバックとして残す。
    ImGui_ImplWin32_EnableDpiAwareness();

    className_ = L"CurvatureBakerWnd";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = &Window::Proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // アイコンは表示サイズに合う画像を .ico から選んで読み込む (大: タスクバー/Alt+Tab, 小: タイトルバー)
    wc.hIcon   = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.lpszClassName = className_.c_str();
    RegisterClassExW(&wc);

    // this を lpParam で渡し、WM_NCCREATE で GWLP_USERDATA に保存する
    hwnd_ = CreateWindowW(className_.c_str(), title, WS_OVERLAPPEDWINDOW, 100, 100, logicalWidth, logicalHeight, nullptr, nullptr, hInst, this);
    if (!hwnd_) throw std::runtime_error("CreateWindow failed");

    // 初期サイズは 96DPI 基準の論理サイズ。ウィンドウがあるモニタの DPI に合わせて拡大する。
    const float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_);
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(logicalWidth * dpi), static_cast<int>(logicalHeight * dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    DragAcceptFiles(hwnd_, TRUE); // ファイルのドラッグ&ドロップを受け付ける
}

void Window::Show(int nShow) {
    ShowWindow(hwnd_, nShow);
    UpdateWindow(hwnd_);
}

void Window::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (hInst_ && !className_.empty()) {
        UnregisterClassW(className_.c_str(), hInst_);
        hInst_ = nullptr;
    }
}

bool Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

LRESULT CALLBACK Window::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->OnMessage(hwnd, msg, wParam, lParam);
}

LRESULT Window::OnMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (cb_.onResize && wParam != SIZE_MINIMIZED) cb_.onResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DPICHANGED: {
        // 別 DPI のモニタへ移動 / 表示倍率の変更。アプリへ倍率を通知し、OS 提案の矩形へ移す。
        if (cb_.onDpiChanged) cb_.onDpiChanged(static_cast<float>(LOWORD(wParam)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI));
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES: {
        // 複数ドロップされた場合は先頭の1つだけを使う
        HDROP        drop = reinterpret_cast<HDROP>(wParam);
        const UINT   len  = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(len, L'\0');
        if (len > 0) DragQueryFileW(drop, 0, path.data(), len + 1);
        DragFinish(drop);
        if (cb_.onDropFile && len > 0) cb_.onDropFile(std::move(path));
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Alt メニュー無効化
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
