#pragma once
#include <Windows.h>

#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "app/AppState.h"
#include "gpu/BakePass.h"
#include "gpu/CurvatureCompute.h"
#include "gpu/GpuMesh.h"
#include "mesh/CurvatureGraph.h"
#include "mesh/Mesh.h"
#include "gpu/Renderer.h"
#include "gpu/ViewPass.h"
#include "platform/Window.h"

#include "imgui.h"

// アプリ本体。ウィンドウ・レンダラ・UI の組み立てと、読み込み/保存ジョブ、メインループを持つ。
class App {
public:
    explicit App( HINSTANCE hInst ) : hInst_( hInst ) {}
    ~App();

    int Run( int nShow );

private:
    // バックグラウンドスレッドでの読み込み結果
    struct LoadResult {
        bool        ok = false;
        std::string err;
        Mesh        base; // ファイルから読み込んだまま
        Mesh        mesh; // スムース法線適用後(GPUへ送る)
        CurvatureGraph graph; // mesh から作った曲率計算用の隣接グラフ
    };
    // 画像保存(エンコード + ファイル書き込み)の結果
    struct SaveResult {
        bool        ok = false;
        std::string message; // 成功: 保存先 / 失敗: 理由
    };

    void InitImGui();
    void InitFonts();
    ImFont* FontFor( Lang lang ) const;
    void ShutdownImGui();
    void ApplyDpiScale( float scale );

    bool PollJobs(); // 状態が変わったら true
    void HandleDrop();
    bool HandleActions( const Actions& act ); // 保存先が決まって保存を始めるべきなら true
    void RenderFrame();
    void StartSaveIfRequested( bool requested );
    void UpdateTexturePixels(); // テクスチャタブのピクセル値表示用に、結果テクスチャを CPU へ読み戻す

    void RequestLoad( const std::string& path ); // 表示を更新してから読み込みを始める
    void StartLoad( std::string path );
    void StartRemesh();

    HINSTANCE hInst_;
    Window    window_;
    Renderer  renderer_;
    GpuMesh   gpuMesh_;
    CurvatureCompute curvature_; // 頂点ごとの曲率 (GPU コンピュート)
    BakePass  bake_;
    ViewPass  view_;

    AppState state_;
    Mesh     baseMesh_; // ファイルから読み込んだままのメッシュ (3Dビューのカメラ計算とメッシュ更新に使う)

    std::future<LoadResult> loadJob_;
    bool                    loadIsRemesh_ = false;
    std::future<SaveResult> saveJob_;

    // 再描画の要否。入力などがあれば kRedrawFrames まで引き上げ、描くたびに減らす。0 の間はメッセージ待ちで眠る。
    // (ImGui は入力の反映に数フレームかかるので、入力後は少し多めに描く)
    static constexpr int kRedrawFrames = 3;
    int                  redraw_ = kRedrawFrames;
    bool                 JobsRunning() const { return state_.loading || state_.saving; }

    std::filesystem::path pendingDrop_; // ドロップされたファイル (次のフレームで処理)
    bool                  hasPendingDrop_ = false;

    // DPI 変更に何度でも追従できるよう、倍率適用前のスタイルを保持する (ScaleAllSizes は累積してしまうため)
    ImGuiStyle baseStyle_;
    bool       imguiReady_ = false;

    // フォント。日本語 / 簡体字中国語 / ハングルは別々のフォントで、どれを先頭にするかで同じ漢字の字形が変わるため、
    // 言語ごとに先頭を入れ替えたセットを用意して切り替える (ラテン文字・キリル文字は日本語セットを使う)。
    // フォントファイルの中身は ImGui に所有させず、ここで1度だけ読んで全セットで共有する。
    std::vector<std::vector<unsigned char>> fontFiles_;
    ImFont* fontJa_ = nullptr;
    ImFont* fontZh_ = nullptr;
    ImFont* fontKo_ = nullptr;
};