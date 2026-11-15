#include "App.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <utility>

#include "Dialogs.h"
#include "ImageWriter.h"
#include "PathUtil.h"
#include "Ui.h"

#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace fs = std::filesystem;

namespace {

// ImGui DX12 バックエンドへ渡す SRV アロケータ
void SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    static_cast<Renderer*>(info->UserData)->AllocSrv(cpu, gpu);
}
void SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    static_cast<Renderer*>(info->UserData)->FreeSrv(cpu, gpu);
}

template <class T>
bool IsReady(std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace

App::~App() = default;

// ---------------------------------------------------------------------------
// 初期化 / 終了
// ---------------------------------------------------------------------------
void App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    baseStyle_  = ImGui::GetStyle(); // 倍率適用前の基準スタイル(StyleColorsDark 済み)
    imguiReady_ = true;
    ApplyDpiScale(ImGui_ImplWin32_GetDpiScaleForHwnd(window_.Handle()));

    // 日本語フォント(1.92以降は動的グリフ読み込みなので範囲指定は不要)
    for (const char* path : {"C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\YuGothR.ttc", "C:\\Windows\\Fonts\\msgothic.ttc"}) {
        if (fs::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, 18.0f);
            break;
        }
    }

    ImGui_ImplWin32_Init(window_.Handle());

    ImGui_ImplDX12_InitInfo ii;
    ii.Device               = renderer_.Device();
    ii.CommandQueue         = renderer_.Queue();
    ii.NumFramesInFlight    = Renderer::kBackBufferCount;
    ii.RTVFormat            = Renderer::kBackFormat;
    ii.DSVFormat            = DXGI_FORMAT_UNKNOWN;
    ii.SrvDescriptorHeap    = renderer_.SrvHeap();
    ii.SrvDescriptorAllocFn = SrvAlloc;
    ii.SrvDescriptorFreeFn  = SrvFree;
    ii.UserData             = &renderer_;
    ImGui_ImplDX12_Init(&ii);
}

void App::ShutdownImGui() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    imguiReady_ = false;
}

void App::ApplyDpiScale(float scale) {
    ImGuiStyle s = baseStyle_;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi    = scale;
    ImGui::GetStyle() = s;
}

// ---------------------------------------------------------------------------
// メインループ
// ---------------------------------------------------------------------------
int App::Run(int nShow) {
    Window::Callbacks cb;
    cb.onResize     = [this](UINT w, UINT h) { renderer_.Resize(w, h); }; // Init 前は Resize 側で無視される
    cb.onDpiChanged = [this](float scale) {
        if (imguiReady_) ApplyDpiScale(scale);
    };
    cb.onDropFile = [this](std::wstring path) {
        pendingDrop_    = fs::path(std::move(path));
        hasPendingDrop_ = true;
    };
    window_.Create(hInst_, L"curvtex", 1400, 850, std::move(cb));

    renderer_.Init(window_.Handle());
    bake_.Init(renderer_);
    view_.Init(renderer_);

    window_.Show(nShow);
    InitImGui();

    while (window_.PumpMessages()) {
        if (window_.IsMinimized()) {
            Sleep(10);
            continue;
        }

        PollJobs();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Actions act;
        DrawUI(state_, bake_, view_, act);
        ImGui::Render();

        HandleDrop(act);
        HandleActions(act); // UI からの要求を処理 (GPU がアイドルのこのタイミングで実行)
        RenderFrame();
        StartSaveIfRequested(act); // 保存 (フレーム完了後)
    }

    // 終了時: 保存中ならファイルが途中で切れないよう完了を待つ
    if (saveJob_.valid()) saveJob_.wait();
    renderer_.WaitIdle();
    ShutdownImGui();
    renderer_.Shutdown();
    window_.Destroy();
    return 0;
}

// ---------------------------------------------------------------------------
// バックグラウンドジョブ
// ---------------------------------------------------------------------------
namespace {

// 重い処理(ファイル解析・頂点構築・法線再計算)用。D3D12 のリソース作成はメインスレッドで行うため CPU 側の処理のみ。
// path が空なら base(読み込み済みメッシュ)をそのまま使い、法線だけを再計算する。
template <class Result>
Result BuildMeshJob(const std::string& path, Mesh base, bool smooth) {
    Result r;
    try {
        if (!path.empty()) {
            Mesh m;
            if (!LoadMesh(PathFromUtf8(path), m, r.err)) return r;
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
}

} // namespace

void App::StartLoad(std::string path) {
    loadIsRemesh_  = false;
    state_.loading = true;
    loadJob_       = std::async(std::launch::async, [path = std::move(path), smooth = state_.smoothNormals]() { return BuildMeshJob<LoadResult>(path, Mesh{}, smooth); });
}

void App::StartRemesh() {
    loadIsRemesh_  = true;
    state_.loading = true;
    loadJob_       = std::async(std::launch::async, [base = baseMesh_, smooth = state_.smoothNormals]() mutable { return BuildMeshJob<LoadResult>(std::string(), std::move(base), smooth); });
}

void App::PollJobs() {
    // ---- 画像保存の完了確認 ----
    if (IsReady(saveJob_)) {
        const SaveResult sr = saveJob_.get();
        state_.saving       = false;
        state_.saveStatus   = sr.ok ? "保存しました: " + sr.message : "保存に失敗しました: " + sr.message;
    }

    // ---- モデル読み込みの完了確認 (GPUはアイドル) ----
    if (IsReady(loadJob_)) {
        LoadResult r   = loadJob_.get();
        state_.loading = false;
        if (r.ok) {
            try {
                gpuMesh_.Upload(renderer_, r.mesh); // D3D12 リソース作成はメインスレッドで
                baseMesh_       = std::move(r.base);
                state_.hasMesh  = true;
                state_.hasBaked = true;
                state_.dirty    = true;
                if (!loadIsRemesh_) {
                    state_.defaultMaxCurv = 4.0f / r.mesh.radius;
                    state_.maxCurv        = state_.defaultMaxCurv;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "読み込み完了: 頂点 %zu / 三角形 %zu / 半径 %.4g", r.mesh.vertices.size(), r.mesh.indices.size() / 3, r.mesh.radius);
                    state_.modelStatus = buf;
                }
            } catch (const std::exception& e) {
                state_.modelStatus = std::string("GPUへの転送に失敗: ") + e.what();
            }
        } else {
            state_.modelStatus = (loadIsRemesh_ ? "メッシュ更新に失敗: " : "読み込み失敗: ") + r.err;
        }
    }
}

// ---------------------------------------------------------------------------
// UI からの要求
// ---------------------------------------------------------------------------
void App::HandleDrop(Actions& act) {
    if (!hasPendingDrop_) return;
    hasPendingDrop_ = false;
    const fs::path dropped = std::move(pendingDrop_);

    if (state_.loading) {
        state_.modelStatus = "読み込み中のため、ドロップされたファイルは無視しました";
    } else if (ModelFormatFromPath(dropped) == ModelFormat::Unknown) {
        state_.modelStatus = "未対応のファイルです（対応: .obj / .gltf / .glb / .fbx）";
    } else {
        state_.modelPath = PathToUtf8(dropped);
        act.load         = true;
    }
}

void App::HandleActions(Actions& act) {
    if (act.browseModel) {
        const std::string p = OpenModelDialog(window_.Handle());
        if (!p.empty()) {
            state_.modelPath = p;
            act.load         = true;
        }
    }
    if (act.load && !state_.modelPath.empty() && !state_.loading) {
        state_.modelStatus = "読み込み中...";
        StartLoad(CleanPath(state_.modelPath));
    } else if (act.remesh && state_.hasMesh && !state_.loading) {
        StartRemesh();
    }
    if (act.applySize) {
        bake_.Resize(static_cast<UINT>(state_.texW), static_cast<UINT>(state_.texH));
        state_.dirty = true;
    }
    if (act.browseSave) {
        const std::string p = SaveImageDialog(window_.Handle(), state_.savePath);
        if (!p.empty()) state_.savePath = p;
    }
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------
void App::RenderFrame() {
    const bool drawView = state_.view3dVisible && state_.hasMesh && state_.hasBaked;
    if (drawView) view_.Resize(static_cast<UINT>(state_.viewReqW), static_cast<UINT>(state_.viewReqH)); // 記録前にサイズ追従

    renderer_.BeginFrame();
    if (state_.dirty && state_.hasMesh) {
        BakePass::Params p;
        p.invRange = 1.0f / std::max(state_.maxCurv, 1e-6f);
        p.mode     = static_cast<uint32_t>(state_.mode);
        p.radius   = static_cast<uint32_t>(state_.sampleRadius);
        p.padding  = static_cast<uint32_t>(state_.padding);
        bake_.Record(gpuMesh_, p);
        state_.dirty = false;
    }
    if (drawView) {
        float mvp[16], view[16];
        state_.camera.Matrices(baseMesh_, static_cast<float>(view_.Width()) / static_cast<float>(view_.Height()), mvp, view);
        view_.Record(gpuMesh_, bake_, mvp, view, state_.lit);
    }
    renderer_.BeginBackbuffer(0.10f, 0.10f, 0.12f);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), renderer_.CommandList());
    renderer_.EndFrame();
}

// GPU からの読み出しだけメインスレッドで行い、PNG エンコードとファイル書き込みはワーカーで実行する。
// 読み出した画素は複製済みなので、保存中にパラメータを変えても出力は変わらない。
void App::StartSaveIfRequested(const Actions& act) {
    if (!act.save || !state_.hasBaked || state_.savePath.empty() || state_.saving) return;

    fs::path out = PathFromUtf8(state_.savePath);
    if (!out.has_extension()) out.replace_extension(L".png");

    std::vector<uint8_t> rgba;
    if (!bake_.Readback(rgba)) {
        state_.saveStatus = "保存に失敗しました（結果の読み出しに失敗）";
        return;
    }
    state_.saveStatus = "保存中: " + PathToUtf8(out);
    state_.saving     = true;
    saveJob_          = std::async(std::launch::async, [out = std::move(out), rgba = std::move(rgba), w = bake_.Width(), h = bake_.Height()]() {
        SaveResult  r;
        std::string err;
        try {
            r.ok      = WriteImage(out, w, h, rgba.data(), err);
            r.message = r.ok ? PathToUtf8(out) : err;
        } catch (const std::exception& e) {
            r.ok      = false;
            r.message = e.what();
        }
        return r;
    });
}
