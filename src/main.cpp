#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include "ImageWriter.h"
#include "Mesh.h"
#include "Renderer.h"

// imgui_impl_win32.h では #if 0 で隠されているので自前で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

namespace fs = std::filesystem;

Renderer* g_renderer = nullptr;

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16
// ---------------------------------------------------------------------------
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int         n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// ---------------------------------------------------------------------------
// ファイルダイアログ
// ---------------------------------------------------------------------------
std::string OpenFileDialog(HWND owner) {
    wchar_t        buf[MAX_PATH * 4] = {};
    OPENFILENAMEW  ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"3Dモデル (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF (*.gltf;*.glb)\0*.gltf;*.glb\0FBX (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return Narrow(buf);
}

std::string SaveFileDialog(HWND owner, const std::string& initial) {
    wchar_t buf[MAX_PATH * 4] = {};
    std::wstring w            = Widen(initial);
    wcsncpy_s(buf, w.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0Bitmap (*.bmp)\0*.bmp\0TGA (*.tga)\0*.tga\0\0";
    // 現在のパスの拡張子に合わせてフィルタを選んでおく (1=PNG, 2=BMP, 3=TGA)
    switch (ImageFormatFromPath(fs::path(w))) {
    case ImageFormat::Bmp: ofn.nFilterIndex = 2; break;
    case ImageFormat::Tga: ofn.nFilterIndex = 3; break;
    default: ofn.nFilterIndex = 1; break;
    }
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    fs::path chosen(buf);
    if (!chosen.has_extension()) { // 拡張子なしで確定した場合は、選択中のフィルタの形式にする
        static const wchar_t* exts[] = {L".png", L".bmp", L".tga"};
        chosen += exts[std::clamp<DWORD>(ofn.nFilterIndex, 1, 3) - 1];
    }
    return Narrow(chosen.wstring());
}

// ---------------------------------------------------------------------------
// アプリ状態
// ---------------------------------------------------------------------------
// バックグラウンドスレッドでの読み込み結果
struct LoadResult {
    bool        ok = false;
    std::string err;
    Mesh        base; // ファイルから読み込んだまま
    Mesh        mesh; // スムース法線適用後(GPUへ送る)
};

struct AppState {
    std::future<LoadResult> job;          // 実行中の読み込み/メッシュ更新ジョブ
    bool                    jobIsRemesh = false;
    bool                    Loading() const { return job.valid(); }

    char  objPath[1024]  = "";
    char  savePath[1024] = "curvature.png";
    int   texW = 1024, texH = 1024;
    int   mode      = 0;      // 0: 符号付き, 1: 絶対値
    int   sampleRadius = 3;   // 曲率差分のサンプル幅(テクセル)
    bool  smoothNormals = true; // スムース法線を再計算するか
    Mesh  baseMesh;           // ファイルから読み込んだままのメッシュ
    float maxCurv   = 4.0f;   // この曲率(1/単位長)で出力が飽和する
    float defaultMaxCurv = 4.0f;
    bool  hasMesh   = false;
    bool  hasBaked  = false;
    bool  dirty     = false;
    std::string modelStatus = "モデル未読み込み";
    std::string saveStatus;
};

// 前後の空白と、エクスプローラの「パスのコピー」で付く引用符を除去
std::string CleanPath(std::string p) {
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t' || p.back() == '"' || p.back() == '\r' || p.back() == '\n')) p.pop_back();
    size_t b = 0;
    while (b < p.size() && (p[b] == ' ' || p[b] == '\t' || p[b] == '"')) ++b;
    return p.substr(b);
}

// 重い処理(ファイル解析・頂点構築・法線再計算)を別スレッドで実行する。
// path が空なら読み込み済みメッシュ(base)の法線だけを再計算する。
// ※ D3D12 のリソース作成はメインスレッドで行うため、ここでは CPU 側の処理のみ。
void StartJob(AppState& s, std::string path, Mesh base, bool smooth) {
    s.jobIsRemesh = path.empty();
    s.job = std::async(std::launch::async, [path = std::move(path), base = std::move(base), smooth]() mutable {
        LoadResult r;
        try {
            if (!path.empty()) {
                Mesh m;
                if (!LoadMesh(fs::path(Widen(path)), m, r.err)) return r;
                r.base = std::move(m);
            } else {
                r.base = std::move(base);
            }
            r.mesh = r.base;
            if (smooth) RecomputeSmoothNormals(r.mesh);
            r.ok = true;
        } catch (const std::exception& e) {
            r.ok  = false;
            r.err = std::string("例外: ") + e.what();
        }
        return r;
    });
}

struct Actions {
    bool remesh = false, browseObj = false, load = false, applySize = false, browseSave = false, save = false;
};

void DrawUI(AppState& s, Renderer& r, Actions& act) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Main", nullptr, flags);

    const float leftW = 380.0f * ImGui::GetStyle().FontScaleDpi;

    // ---- 左: 設定パネル ----
    ImGui::BeginChild("Settings", ImVec2(leftW, 0), ImGuiChildFlags_Borders);

    ImGui::SeparatorText("モデル");
    const bool loading = s.Loading();
    ImGui::BeginDisabled(loading);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##obj", s.objPath, sizeof(s.objPath));
    if (ImGui::Button("参照...")) act.browseObj = true;
    ImGui::SameLine();
    if (ImGui::Button("読み込み")) act.load = true;
    if (ImGui::Checkbox("スムース法線を再計算", &s.smoothNormals)) act.remesh = true;
    ImGui::EndDisabled();
    ImGui::TextDisabled("位置が同じ頂点を溶接して法線を平滑化します");
    if (loading) {
        const char spin[] = "|/-\\";
        ImGui::Text("%s 処理中...", std::string(1, spin[static_cast<int>(ImGui::GetTime() * 8.0) & 3]).c_str());
    }
    ImGui::TextWrapped("%s", s.modelStatus.c_str());

    ImGui::SeparatorText("テクスチャサイズ");
    ImGui::InputInt("幅", &s.texW, 0, 0);
    ImGui::InputInt("高さ", &s.texH, 0, 0);
    s.texW = std::clamp(s.texW, 16, 4096);
    s.texH = std::clamp(s.texH, 16, 4096);
    for (int sz : {256, 512, 1024, 2048, 4096}) {
        char label[16];
        std::snprintf(label, sizeof(label), "%d", sz);
        if (ImGui::SmallButton(label)) {
            s.texW = s.texH = sz;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::Button("サイズを適用")) act.applySize = true;
    ImGui::SameLine();
    ImGui::TextDisabled("現在: %u x %u", r.BakeWidth(), r.BakeHeight());

    ImGui::SeparatorText("曲率");
    if (ImGui::RadioButton("符号付き (0.5=平坦, 凸>0.5, 凹<0.5)", &s.mode, 0)) s.dirty = true;
    if (ImGui::RadioButton("絶対値 (0=平坦)", &s.mode, 1)) s.dirty = true;
    ImGui::Text("サンプル幅 (テクセル)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##radius", &s.sampleRadius, 1, 16)) s.dirty = true;
    ImGui::TextDisabled("大きいほど滑らか。小さいと三角形ごとの段差が出ます");
    ImGui::Text("最大曲率 (1/単位長)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##kmax", &s.maxCurv, 1e-3f, 1e4f, "%.4f", ImGuiSliderFlags_Logarithmic)) s.dirty = true;
    if (ImGui::Button("初期値に戻す")) {
        s.maxCurv = s.defaultMaxCurv;
        s.dirty   = true;
    }
    ImGui::TextWrapped("この曲率で出力が0/1に飽和します。半径Rの球の曲率は 1/R です。");

    ImGui::SeparatorText("画像保存 (PNG / BMP / TGA)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##save", s.savePath, sizeof(s.savePath));
    if (ImGui::Button("参照...##save")) act.browseSave = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.hasBaked);
    if (ImGui::Button("保存")) act.save = true;
    ImGui::EndDisabled();
    if (!s.saveStatus.empty()) ImGui::TextWrapped("%s", s.saveStatus.c_str());

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 右: プレビュー ----
    ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (s.hasBaked) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float  w     = static_cast<float>(r.BakeWidth());
        const float  h     = static_cast<float>(r.BakeHeight());
        const float  scale = std::min(avail.x / w, avail.y / h);
        ImGui::Image(static_cast<ImTextureID>(r.ResultSrvGpu().ptr), ImVec2(w * scale, h * scale));
    } else {
        ImGui::TextDisabled("モデルを読み込むと、ここに曲率テクスチャが表示されます。");
    }
    ImGui::EndChild();

    ImGui::End();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_renderer && wParam != SIZE_MINIMIZED) {
            g_renderer->Resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Alt メニュー無効化
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ImGui DX12 バックエンドへ渡すSRVアロケータ
void SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    static_cast<Renderer*>(info->UserData)->AllocSrv(cpu, gpu);
}
void SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    static_cast<Renderer*>(info->UserData)->FreeSrv(cpu, gpu);
}

int Run(HINSTANCE hInst, int nShow) {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CurvatureBakerWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Curvature Baker", WS_OVERLAPPEDWINDOW, 100, 100, 1400, 850, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) throw std::runtime_error("CreateWindow failed");

    Renderer renderer;
    renderer.Init(hwnd);
    g_renderer = &renderer;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    // ---- ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    const float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpi);
    style.FontScaleDpi = dpi;

    // 日本語フォント(1.92以降は動的グリフ読み込みなので範囲指定は不要)
    for (const char* path : {"C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\YuGothR.ttc", "C:\\Windows\\Fonts\\msgothic.ttc"}) {
        if (fs::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, 18.0f);
            break;
        }
    }

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo ii;
    ii.Device               = renderer.Device();
    ii.CommandQueue         = renderer.Queue();
    ii.NumFramesInFlight    = Renderer::kBackBufferCount;
    ii.RTVFormat            = Renderer::kBackFormat;
    ii.DSVFormat            = DXGI_FORMAT_UNKNOWN;
    ii.SrvDescriptorHeap    = renderer.SrvHeap();
    ii.SrvDescriptorAllocFn = SrvAlloc;
    ii.SrvDescriptorFreeFn  = SrvFree;
    ii.UserData             = &renderer;
    ImGui_ImplDX12_Init(&ii);

    AppState state;

    // ---- メインループ ----
    MSG  msg{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(hwnd)) {
            Sleep(10);
            continue;
        }

        // ---- バックグラウンド読み込みの完了確認 (GPUはアイドル) ----
        if (state.job.valid() && state.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LoadResult r = state.job.get();
            if (r.ok) {
                try {
                    renderer.SetMesh(r.mesh); // D3D12 リソース作成はメインスレッドで
                    state.baseMesh = std::move(r.base);
                    state.hasMesh  = true;
                    state.hasBaked = true;
                    state.dirty    = true;
                    if (!state.jobIsRemesh) {
                        state.defaultMaxCurv = 4.0f / r.mesh.radius;
                        state.maxCurv        = state.defaultMaxCurv;
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "読み込み完了: 頂点 %zu / 三角形 %zu / 半径 %.4g", r.mesh.vertices.size(), r.mesh.indices.size() / 3, r.mesh.radius);
                        state.modelStatus = buf;
                    }
                } catch (const std::exception& e) {
                    state.modelStatus = std::string("GPUへの転送に失敗: ") + e.what();
                }
            } else {
                state.modelStatus = (state.jobIsRemesh ? "メッシュ更新に失敗: " : "読み込み失敗: ") + r.err;
            }
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Actions act;
        DrawUI(state, renderer, act);
        ImGui::Render();

        // ---- UI からの要求を処理 (GPUがアイドルのこのタイミングで実行) ----
        if (act.browseObj) {
            std::string p = OpenFileDialog(hwnd);
            if (!p.empty()) {
                std::snprintf(state.objPath, sizeof(state.objPath), "%s", p.c_str());
                act.load = true;
            }
        }
        if (act.load && state.objPath[0] != '\0' && !state.Loading()) {
            state.modelStatus = "読み込み中...";
            StartJob(state, CleanPath(state.objPath), Mesh{}, state.smoothNormals);
        } else if (act.remesh && state.hasMesh && !state.Loading()) {
            StartJob(state, std::string(), state.baseMesh, state.smoothNormals);
        }
        if (act.applySize) {
            renderer.SetBakeSize(static_cast<UINT>(state.texW), static_cast<UINT>(state.texH));
            state.dirty = true;
        }
        if (act.browseSave) {
            std::string p = SaveFileDialog(hwnd, state.savePath);
            if (!p.empty()) std::snprintf(state.savePath, sizeof(state.savePath), "%s", p.c_str());
        }

        // ---- 描画 ----
        renderer.BeginFrame();
        if (state.dirty && state.hasMesh) {
            renderer.RecordBake(1.0f / std::max(state.maxCurv, 1e-6f), static_cast<uint32_t>(state.mode), static_cast<uint32_t>(state.sampleRadius));
            state.dirty = false;
        }
        renderer.BeginBackbuffer(0.10f, 0.10f, 0.12f);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), renderer.CommandList());
        renderer.EndFrame();

        // ---- 保存 (フレーム完了後) ----
        if (act.save && state.hasBaked && state.savePath[0] != '\0') {
            fs::path out(Widen(state.savePath));
            if (!out.has_extension()) out.replace_extension(L".png");
            std::vector<uint8_t> rgba;
            std::string          err;
            if (!renderer.ReadbackResult(rgba)) {
                state.saveStatus = "保存に失敗しました（結果の読み出しに失敗）";
            } else if (WriteImage(out, renderer.BakeWidth(), renderer.BakeHeight(), rgba.data(), err)) {
                state.saveStatus = "保存しました: " + Narrow(out.wstring());
            } else {
                state.saveStatus = "保存に失敗しました: " + err;
            }
        }
    }

    renderer.WaitIdle();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_renderer = nullptr;
    renderer.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    return 0;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    try {
        return Run(hInst, nShow);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Curvature Baker - Error", MB_ICONERROR | MB_OK);
        return 1;
    }
}
