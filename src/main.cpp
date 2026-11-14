#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
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

#include <DirectXMath.h>

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
    int   padding      = 4;   // UV島の境界を塗り広げる幅(テクセル)。0 で無効
    bool  smoothNormals = true; // スムース法線を再計算するか
    Mesh  baseMesh;           // ファイルから読み込んだままのメッシュ
    float maxCurv   = 4.0f;   // この曲率(1/単位長)で出力が飽和する
    float defaultMaxCurv = 4.0f;
    bool  hasMesh   = false;
    bool  hasBaked  = false;
    bool  dirty     = false;
    std::string modelStatus = "モデル未読み込み";
    std::string saveStatus;

    // ---- 3Dビュー (オービットカメラ。距離・移動量はモデル半径を1とした単位) ----
    float yaw = 0.6f, pitch = 0.35f, dist = 3.0f;
    float target[3] = {0, 0, 0}; // 注視点オフセット (モデル中心からの移動量)
    bool  lit       = true;
    bool  view3dVisible = false; // 今フレーム 3D タブが表示されているか (DrawUI が設定)
    int   viewReqW = 0, viewReqH = 0; // 3D 表示領域のピクセルサイズ (DrawUI が設定)
};

constexpr float kFovY = DirectX::XM_PIDIV4;

void ResetCamera(AppState& s) {
    s.yaw = 0.6f;
    s.pitch = 0.35f;
    s.dist = 3.0f;
    s.target[0] = s.target[1] = s.target[2] = 0.0f;
}

// 注視点→カメラの向きから、カメラの右・上ベクトルを求める (Y-up)
void CameraBasis(const AppState& s, DirectX::XMFLOAT3& right, DirectX::XMFLOAT3& up) {
    using namespace DirectX;
    const XMVECTOR eyeDir = XMVectorSet(std::cos(s.pitch) * std::sin(s.yaw), std::sin(s.pitch), std::cos(s.pitch) * std::cos(s.yaw), 0.0f);
    const XMVECTOR fwd    = XMVectorNegate(eyeDir);
    const XMVECTOR r      = XMVector3Normalize(XMVector3Cross(fwd, XMVectorSet(0, 1, 0, 0)));
    const XMVECTOR u      = XMVector3Cross(r, fwd);
    XMStoreFloat3(&right, r);
    XMStoreFloat3(&up, u);
}

// モデル行列(中心を原点へ・半径1へ正規化) × ビュー × 射影。HLSL 側は row_major で受けるので転置は不要。
void BuildCamera(const AppState& s, const Mesh& mesh, float aspect, float mvp[16], float view[16]) {
    using namespace DirectX;
    const XMMATRIX model = XMMatrixTranslation(-mesh.center[0], -mesh.center[1], -mesh.center[2]) * XMMatrixScaling(1.0f / mesh.radius, 1.0f / mesh.radius, 1.0f / mesh.radius);
    const XMVECTOR tgt   = XMVectorSet(s.target[0], s.target[1], s.target[2], 1.0f);
    const XMVECTOR eye   = XMVectorAdd(tgt, XMVectorScale(XMVectorSet(std::cos(s.pitch) * std::sin(s.yaw), std::sin(s.pitch), std::cos(s.pitch) * std::cos(s.yaw), 0.0f), s.dist));
    const XMMATRIX v     = XMMatrixLookAtRH(eye, tgt, XMVectorSet(0, 1, 0, 0));
    const XMMATRIX p     = XMMatrixPerspectiveFovRH(kFovY, aspect, 0.02f, 200.0f);
    XMFLOAT4X4     fm, fv;
    XMStoreFloat4x4(&fm, model * v * p);
    XMStoreFloat4x4(&fv, v);
    std::memcpy(mvp, &fm, sizeof(fm));
    std::memcpy(view, &fv, sizeof(fv));
}

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
    ImGui::Text("パディング (テクセル)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##padding", &s.padding, 0, 16)) s.dirty = true;
    ImGui::TextDisabled("UV島の外側を最寄りの値で塗り広げ、継ぎ目の黒い線を防ぎます");
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

    // ---- 右: プレビュー (タブで 3Dビュー / テクスチャ を切り替え) ----
    ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    s.view3dVisible = false;
    if (ImGui::BeginTabBar("PreviewTabs")) {
        // ---- 3Dビュー ----
        if (ImGui::BeginTabItem("3Dビュー")) {
            if (s.hasBaked) {
                s.view3dVisible = true;
                ImGui::Checkbox("ライティング", &s.lit);
                ImGui::SameLine();
                if (ImGui::Button("カメラをリセット")) ResetCamera(s);
                ImGui::SameLine();
                ImGui::TextDisabled("左ドラッグ: 回転 / 右・中ドラッグ: 移動 / ホイール: ズーム");

                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x >= 16.0f && avail.y >= 16.0f) {
                    s.viewReqW = static_cast<int>(avail.x);
                    s.viewReqH = static_cast<int>(avail.y);

                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::Image(static_cast<ImTextureID>(r.ViewSrvGpu().ptr), avail);
                    ImGui::SetCursorScreenPos(p);
                    ImGui::InvisibleButton("##view3d", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

                    const ImGuiIO& io = ImGui::GetIO();
                    if (ImGui::IsItemActive()) {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            // オービット (ピッチは真上/真下の手前で止めて反転を防ぐ)
                            s.yaw -= io.MouseDelta.x * 0.01f;
                            s.pitch = std::clamp(s.pitch + io.MouseDelta.y * 0.01f, -1.55f, 1.55f);
                        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                            // パン: 注視点での 1ピクセル あたりの距離でシーンがカーソルに追従する量だけ動かす
                            DirectX::XMFLOAT3 right, up;
                            CameraBasis(s, right, up);
                            const float k = 2.0f * s.dist * std::tan(kFovY * 0.5f) / std::max(avail.y, 1.0f);
                            for (int i = 0; i < 3; ++i) {
                                const float rv = (i == 0) ? right.x : (i == 1) ? right.y : right.z;
                                const float uv = (i == 0) ? up.x : (i == 1) ? up.y : up.z;
                                s.target[i] += (-rv * io.MouseDelta.x + uv * io.MouseDelta.y) * k;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                        s.dist = std::clamp(s.dist * std::pow(0.9f, io.MouseWheel), 0.2f, 50.0f);
                    }
                }
            } else {
                ImGui::TextDisabled("モデルを読み込むと、ここに曲率テクスチャを貼った3Dモデルが表示されます。");
            }
            ImGui::EndTabItem();
        }

        // ---- テクスチャ ----
        if (ImGui::BeginTabItem("テクスチャ")) {
            if (s.hasBaked) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float  w     = static_cast<float>(r.BakeWidth());
                const float  h     = static_cast<float>(r.BakeHeight());
                const float  scale = std::min(avail.x / w, avail.y / h);
                ImGui::Image(static_cast<ImTextureID>(r.ResultSrvGpu().ptr), ImVec2(w * scale, h * scale));
            } else {
                ImGui::TextDisabled("モデルを読み込むと、ここに曲率テクスチャが表示されます。");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();
}

// DPI 変更に何度でも追従できるよう、倍率適用前のスタイルを保持しておく。
// (ScaleAllSizes は累積してしまうので、毎回「基準スタイル」から作り直す)
ImGuiStyle g_baseStyle;
bool       g_styleReady = false;

void ApplyDpiScale(float scale) {
    ImGuiStyle s = g_baseStyle;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi    = scale;
    ImGui::GetStyle() = s;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_renderer && wParam != SIZE_MINIMIZED) {
            g_renderer->Resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_DPICHANGED: {
        // 別 DPI のモニタへ移動 / 表示倍率の変更。UI の倍率を更新し、OS 提案の矩形へ移す。
        if (g_styleReady) ApplyDpiScale(static_cast<float>(LOWORD(wParam)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI));
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
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

// ImGui DX12 バックエンドへ渡すSRVアロケータ
void SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    static_cast<Renderer*>(info->UserData)->AllocSrv(cpu, gpu);
}
void SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    static_cast<Renderer*>(info->UserData)->FreeSrv(cpu, gpu);
}

int Run(HINSTANCE hInst, int nShow) {
    // 通常は curvtex.manifest(PerMonitorV2) で宣言済み。マニフェストが無い場合のフォールバックとして残す。
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

    // 初期サイズは 96DPI 基準の論理サイズ。ウィンドウがあるモニタの DPI に合わせて拡大する。
    const float initialDpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(1400 * initialDpi), static_cast<int>(850 * initialDpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

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

    g_baseStyle  = ImGui::GetStyle(); // 倍率適用前の基準スタイル(StyleColorsDark 済み)
    g_styleReady = true;
    ApplyDpiScale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

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
        const bool drawView = state.view3dVisible && state.hasMesh && state.hasBaked;
        if (drawView) renderer.ResizeView(static_cast<UINT>(state.viewReqW), static_cast<UINT>(state.viewReqH)); // 記録前にサイズ追従
        renderer.BeginFrame();
        if (state.dirty && state.hasMesh) {
            renderer.RecordBake(1.0f / std::max(state.maxCurv, 1e-6f), static_cast<uint32_t>(state.mode), static_cast<uint32_t>(state.sampleRadius), static_cast<uint32_t>(state.padding));
            state.dirty = false;
        }
        if (drawView) {
            float mvp[16], view[16];
            BuildCamera(state, state.baseMesh, static_cast<float>(renderer.ViewWidth()) / static_cast<float>(renderer.ViewHeight()), mvp, view);
            renderer.RecordView(mvp, view, state.lit);
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
