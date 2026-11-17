#include "app/App.h"
#include "gpu/UiPointSampler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <utility>

#include "platform/Dialogs.h"
#include "image/ImageWriter.h"
#include "platform/PathUtil.h"
#include "ui/Ui.h"

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

    InitFonts();

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
    try { // テクスチャを最近傍で描くための ImGui 用パイプライン。作れなければ、補間なしの表示だけ使えない (線形補間のまま描く)
        UiPointSampler::Instance().Init(renderer_.Device(), Renderer::kBackFormat);
    } catch (const std::exception&) {
        UiPointSampler::Instance().Shutdown();
    }
}

// 1.92 以降は動的グリフ読み込みなので、グリフの範囲指定は不要。
void App::InitFonts() {
    ImGuiIO& io = ImGui::GetIO();

    enum Script { kJa, kZh, kZhHant, kKo, kThai, kLatin, kScriptCount };
    // 各文字種の候補 (先に見つかったものを使う)。無い文字種は読み飛ばす。
    const std::vector<const char*> candidates[kScriptCount] = {
        {"C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\YuGothR.ttc", "C:\\Windows\\Fonts\\msgothic.ttc"},
        {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simsun.ttc"},
        {"C:\\Windows\\Fonts\\msjh.ttc", "C:\\Windows\\Fonts\\mingliu.ttc"},
        {"C:\\Windows\\Fonts\\malgun.ttf", "C:\\Windows\\Fonts\\gulim.ttc"},
        {"C:\\Windows\\Fonts\\LeelawUI.ttf", "C:\\Windows\\Fonts\\Leelawad.ttf", "C:\\Windows\\Fonts\\tahoma.ttf"}, // タイ語
        {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"}, // ベトナム語・トルコ語などの拡張ラテン文字
    };

    fontFiles_.clear();
    fontFiles_.reserve(kScriptCount);
    int fileIndex[kScriptCount];
    for (int& i : fileIndex) i = -1;
    for (int sc = 0; sc < kScriptCount; ++sc) {
        for (const char* path : candidates[sc]) {
            std::ifstream ifs(fs::path(path), std::ios::binary | std::ios::ate);
            if (!ifs) continue;
            const std::streamsize size = ifs.tellg();
            if (size <= 0) continue;
            std::vector<unsigned char> data(static_cast<size_t>(size));
            ifs.seekg(0);
            if (!ifs.read(reinterpret_cast<char*>(data.data()), size)) continue;
            fileIndex[sc] = static_cast<int>(fontFiles_.size());
            fontFiles_.push_back(std::move(data));
            break;
        }
    }

    // order の先頭を主フォントにし、残りを不足グリフの補完として結合する。
    // どの言語の組でも全文字種を含めておく (言語メニューの各言語名と、モデル内の名前を、どの言語で動いていても表示できるように)。
    auto addSet = [&](std::initializer_list<int> order) -> ImFont* {
        ImFont* primary = nullptr;
        for (int sc : order) {
            if (fileIndex[sc] < 0) continue;
            std::vector<unsigned char>& data = fontFiles_[static_cast<size_t>(fileIndex[sc])];
            ImFontConfig                cfg;
            cfg.FontDataOwnedByAtlas = false; // 中身は fontFiles_ が持つ (複数セットで共有)
            cfg.MergeMode            = primary != nullptr;
            ImFont* f                = io.Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), 18.0f, &cfg);
            if (!primary) primary = f;
        }
        return primary ? primary : io.Fonts->AddFontDefault(); // 日本語フォントが全く無い環境でも、英語などは表示できる
    };
    fontJa_     = addSet({kJa, kLatin, kThai, kZh, kZhHant, kKo});
    fontZh_     = addSet({kZh, kLatin, kThai, kJa, kZhHant, kKo});
    fontZhHant_ = addSet({kZhHant, kLatin, kThai, kZh, kJa, kKo});
    fontKo_     = addSet({kKo, kLatin, kThai, kJa, kZh, kZhHant});
    fontTh_     = addSet({kThai, kLatin, kJa, kZh, kZhHant, kKo});
    fontLatin_  = addSet({kLatin, kThai, kJa, kZh, kZhHant, kKo});
}

ImFont* App::FontFor(int lang) const {
    switch (LangFontKind(lang)) {
    case FontKind::Zh: return fontZh_;
    case FontKind::ZhHant: return fontZhHant_;
    case FontKind::Ko: return fontKo_;
    case FontKind::Th: return fontTh_;
    case FontKind::Latin: return fontLatin_; // 拡張ラテン文字 (ベトナム語の声調記号など) を含むフォントを先頭にする
    default: return fontJa_; // 日本語・英語・欧州言語 (ラテン文字・キリル文字は日本語フォントにある)
    }
}

void App::ShutdownImGui() {
    UiPointSampler::Instance().Shutdown();
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
    cb.onSizingFrame = [this]() {
        if (imguiReady_ && !window_.IsMinimized()) RenderUiFrame(); // 枠をドラッグしている間は、OS のループの中から描く
    };
    cb.onDropFile = [this](std::wstring path) {
        pendingDrop_    = fs::path(std::move(path));
        hasPendingDrop_ = true;
    };
    window_.Create(hInst_, L"curvtex", 1400, 850, std::move(cb));

    renderer_.Init(window_.Handle());
    curvature_.Init(renderer_);
    bake_.Init(renderer_);
    view_.Init(renderer_);

    window_.Show(nShow);
    InitImGui();

    constexpr DWORD kJobPollMs = 50; // ジョブ実行中に完了を確認する間隔 (最小化中など、描画しない間)
    for (;;) {
        bool hadMessage = false;
        if (!window_.PumpMessages(hadMessage)) break;
        if (hadMessage) redraw_ = kRedrawFrames;

        if (window_.IsMinimized()) {
            if (PollJobs()) redraw_ = kRedrawFrames;
            window_.WaitForMessage(JobsRunning() ? kJobPollMs : INFINITE);
            continue;
        }

        if (PollJobs()) redraw_ = kRedrawFrames;
        // ジョブ実行中(スピナーの表示と完了確認)・再ベイク待ち・ドロップ待ちは、入力が無くても描く
        if (JobsRunning() || (state_.hasMesh && state_.AnyDirty()) || hasPendingDrop_) redraw_ = std::max(redraw_, 1); // メッシュ無しではベイクされないので、フラグだけ立っていても無視する

        if (redraw_ == 0) {
            // 何も変わっていないので、入力が来るまで眠る。テキスト入力中だけはカーソル点滅のため定期的に起きる。
            const bool blink = ImGui::GetIO().WantTextInput;
            if (!window_.WaitForMessage(blink ? 250 : INFINITE)) redraw_ = 1;
            continue;
        }
        --redraw_;

        RenderUiFrame();
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
// 1フレーム (メインループと、サイズ変更中の WM_SIZE から呼ぶ)
// ---------------------------------------------------------------------------
void App::RenderUiFrame() {
    if (inFrame_) return;
    inFrame_ = true;
    struct Reset { bool& f; ~Reset() { f = false; } } reset{inFrame_};

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Actions act;
    ImGui::PushFont(FontFor(GetLang()), 0.0f); // 0.0f = サイズはそのまま
    DrawUI(state_, bake_, view_, act);
    ImGui::PopFont();
    ImGui::Render();

    HandleDrop();
    const bool saveNow = HandleActions(act); // UI からの要求を処理 (GPU がアイドルのこのタイミングで実行)
    RenderFrame();
    UpdateTexturePixels();
    StartSaveIfRequested(saveNow); // 保存 (フレーム完了後)
}

// ---------------------------------------------------------------------------
// バックグラウンドジョブ
// ---------------------------------------------------------------------------
namespace {

// 重い処理(ファイル解析・頂点構築・法線再計算)用。D3D12 のリソース作成はメインスレッドで行うため CPU 側の処理のみ。
// path が空なら scene(読み込み済みの全パーツ)から sel の選択でメッシュを作り直す。path があればファイルを読み込み、全パーツを選択する。
template <class Result>
Result BuildMeshJob(const std::string& path, std::shared_ptr<const ModelScene> scene, ModelSelection sel, bool smooth) {
    Result r;
    try {
        if (!path.empty()) {
            auto loaded = std::make_shared<ModelScene>();
            if (!LoadScene(PathFromUtf8(path), *loaded, r.err)) return r;
            sel   = SelectAll(*loaded);
            scene = std::move(loaded);
        }
        if (!scene) {
            r.err = Tr(Str::ErrSelEmpty);
            return r;
        }
        r.scene = scene;
        r.sel   = sel;
        if (!BuildMesh(*scene, sel, r.base, r.err)) return r;
        r.mesh = r.base;
        if (smooth) RecomputeSmoothNormals(r.mesh);
        r.graph = BuildCurvatureGraph(r.mesh); // 隣接グラフ(重いので読み込みと同じワーカーで作る)
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok  = false;
        r.err = std::string(Tr(Str::ExceptionPrefix)) + e.what();
    }
    return r;
}

} // namespace

void App::StartLoad(std::string path) {
    loadIsRemesh_  = false;
    state_.loading = true;
    loadJob_       = std::async(std::launch::async, [path = std::move(path), smooth = state_.smoothNormals]() { return BuildMeshJob<LoadResult>(path, nullptr, ModelSelection{}, smooth); });
}

void App::StartRemesh(const ModelSelection& sel) {
    loadIsRemesh_  = true;
    state_.loading = true;
    loadJob_       = std::async(std::launch::async, [scene = state_.scene, sel, smooth = state_.smoothNormals]() { return BuildMeshJob<LoadResult>(std::string(), scene, sel, smooth); });
}

bool App::PollJobs() {
    bool changed = false;

    // ---- 画像保存の完了確認 ----
    if (IsReady(saveJob_)) {
        changed = true;
        const SaveResult sr = saveJob_.get();
        state_.saving       = false;
        state_.saveStatus   = StatusMsg{sr.ok ? Str::StSaved : Str::StSaveFail, sr.message};
    }

    // ---- モデル読み込みの完了確認 (GPUはアイドル) ----
    if (IsReady(loadJob_)) {
        changed        = true;
        LoadResult r   = loadJob_.get();
        state_.loading = false;
        if (r.ok) {
            try {
                gpuMesh_.Upload(renderer_, r.mesh); // D3D12 リソース作成はメインスレッドで
                curvature_.SetGraph(r.graph);
                baseMesh_        = std::move(r.base);
                state_.scene     = r.scene;
                state_.selApplied = r.sel;
                if (!loadIsRemesh_) state_.sel = r.sel; // 新しいモデルでは選択を全選択に戻す (メッシュ更新では編集中の選択を保つ)
                state_.hasMesh  = true;
                state_.hasBaked = true;
                state_.dirtyGeometry = true;
                if (!loadIsRemesh_) { // 新しいモデルのときだけ、曲率の最大値を初期化する (選択の変更では保つ)
                    state_.defaultMaxCurv = 4.0f / r.mesh.radius;
                    state_.maxCurv        = state_.defaultMaxCurv;
                }
                StatusMsg m;
                m.id               = Str::StLoaded;
                m.verts            = r.mesh.vertices.size();
                m.tris             = r.mesh.indices.size() / 3;
                m.radius           = r.mesh.radius;
                state_.modelStatus = m;
            } catch (const std::exception& e) {
                state_.modelStatus = StatusMsg{Str::StGpuFail, e.what()};
            }
        } else {
            state_.modelStatus = StatusMsg{loadIsRemesh_ ? Str::StRemeshFail : Str::StLoadFail, r.err};
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// UI からの要求
// ---------------------------------------------------------------------------
void App::RequestLoad(const std::string& path) {
    state_.modelPath   = path;
    state_.modelStatus = StatusMsg{Str::StLoading};
    StartLoad(CleanPath(path));
}

void App::HandleDrop() {
    if (!hasPendingDrop_) return;
    hasPendingDrop_ = false;
    const fs::path dropped = std::move(pendingDrop_);

    if (state_.loading) {
        state_.modelStatus = StatusMsg{Str::StDropBusy};
    } else if (ModelFormatFromPath(dropped) == ModelFormat::Unknown) {
        state_.modelStatus = StatusMsg{Str::StDropUnsupported};
    } else {
        RequestLoad(PathToUtf8(dropped));
    }
}

bool App::HandleActions(const Actions& act) {
    if (act.exitApp) PostMessageW(window_.Handle(), WM_CLOSE, 0, 0); // 通常のクローズと同じ経路 (WM_DESTROY → WM_QUIT) で終了する

    if (act.openFile && !state_.loading) {
        const std::string p = OpenModelDialog(window_.Handle());
        if (!p.empty()) RequestLoad(p);
    }
    if (act.remesh && state_.hasMesh && !state_.loading) StartRemesh(state_.selApplied); // 編集中の選択はまだ反映しない
    if (act.applySelection && state_.hasMesh && !state_.loading) StartRemesh(state_.sel);
    if (act.applySize) {
        bake_.Resize(static_cast<UINT>(state_.texW), static_cast<UINT>(state_.texH));
        state_.dirtyGeometry = true;
    }
    if (act.saveFile && state_.hasBaked && !state_.saving) {
        const std::string p = SaveImageDialog(window_.Handle(), state_.savePath);
        if (!p.empty()) {
            state_.savePath = p;
            return true; // 画素の読み出しはフレーム描画の後で行う
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------
void App::RenderFrame() {
    const bool drawView = state_.view3dVisible && state_.hasMesh && state_.hasBaked;
    if (drawView) {
        // 記録前に品質設定とサイズへ追従する (どちらも変わったときだけ作り直す)
        view_.SetQuality(static_cast<UINT>(state_.aaSamples), static_cast<UINT>(state_.anisotropy));
        view_.PrepareTexture(bake_.Width(), bake_.Height());
        view_.Resize(static_cast<UINT>(state_.viewReqW), static_cast<UINT>(state_.viewReqH));
    }

    renderer_.BeginFrame();
    if (state_.hasMesh && state_.AnyDirty()) {
        // 上流が変わったら下流も作り直す。
        //   曲率の計算 → (平滑化 → ラスタライズ) → パディング → シェーディング
        if (state_.dirtyGeometry) {
            curvature_.RecordRaw();
            state_.dirtySmoothing = state_.dirtyPadding = true;
        }
        if (state_.dirtySmoothing) {
            curvature_.RecordSmoothing(static_cast<uint32_t>(state_.smoothing));
            bake_.RecordRaster(gpuMesh_, curvature_);
            state_.dirtyShading = true;
        }
        if (state_.dirtyPadding) {
            bake_.RecordPadding(static_cast<uint32_t>(state_.padding));
            state_.dirtyShading = true;
        }
        BakePass::Params p;
        p.invRange = 1.0f / std::max(state_.maxCurv, 1e-6f);
        p.mode     = static_cast<uint32_t>(state_.mode);
        p.padding  = static_cast<uint32_t>(state_.padding);
        bake_.RecordShading(p);
        state_.dirtyGeometry = state_.dirtySmoothing = state_.dirtyPadding = state_.dirtyShading = false;
    }
    if (drawView) {
        float mvp[16], view[16];
        state_.camera.Matrices(baseMesh_, static_cast<float>(view_.Width()) / static_cast<float>(view_.Height()), mvp, view);
        view_.Record(gpuMesh_, bake_, mvp, view, state_.lit);
    }
    renderer_.BeginBackbuffer(0.10f, 0.10f, 0.12f);
    UiPointSampler::Instance().BeginFrame(renderer_.CommandList(), ImGui::GetDrawData());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), renderer_.CommandList());
    renderer_.EndFrame();
}

// テクスチャタブでカーソルがテクスチャ上にあり、読み戻し済みの内容が古いときだけ読み戻す。
// (読み戻しは GPU の完了待ちを伴うので、スライダー操作中など、ピクセル値が要らない間は行わない)
void App::UpdateTexturePixels() {
    TexPixels& tp = state_.texPixels;
    if (!state_.texPixelsWanted || !state_.hasBaked || tp.version == bake_.Version()) return;
    if (!bake_.Readback(tp.rgba)) return;
    tp.version = bake_.Version();
    tp.width   = static_cast<int>(bake_.Width());
    tp.height  = static_cast<int>(bake_.Height());
    tp.mode    = state_.mode;
    tp.maxCurv = std::max(state_.maxCurv, 1e-6f);
    redraw_    = std::max(redraw_, 1); // ツールチップに値を反映するため、もう1フレーム描く
}

// GPU からの読み出しだけメインスレッドで行い、PNG エンコードとファイル書き込みはワーカーで実行する。
// 読み出した画素は複製済みなので、保存中にパラメータを変えても出力は変わらない。
void App::StartSaveIfRequested(bool requested) {
    if (!requested || !state_.hasBaked || state_.savePath.empty() || state_.saving) return;

    fs::path out = PathFromUtf8(state_.savePath);
    if (!out.has_extension()) out.replace_extension(L".png");

    std::vector<uint8_t> rgba;
    if (!bake_.Readback(rgba)) {
        state_.saveStatus = StatusMsg{Str::StReadbackFail};
        return;
    }
    state_.saveStatus = StatusMsg{Str::StSaving, PathToUtf8(out)};
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
