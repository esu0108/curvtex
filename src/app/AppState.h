#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "i18n/Strings.h"
#include "mesh/Mesh.h"
#include "ui/Camera.h"

// テクスチャタブの表示状態 (ズーム・パン)
struct TexViewState {
    float zoom = 1.0f;              // 1.0 = 表示領域にフィット
    float offX = 0.0f, offY = 0.0f; // 表示領域の中心から見たテクスチャ中心のずれ (画面px)
    int   texW = 0, texH = 0;       // 前回表示したテクスチャのサイズ (変わったらリセットする)
    bool  linear = true;            // true: 線形補間 / false: 最近傍 (Reset では変えない)
    void  Reset() { zoom = 1.0f; offX = offY = 0.0f; }
};

// ピクセル値の表示用に、結果テクスチャを CPU へ読み戻したもの (RGBA8, 行 0 が画像の上端)。
struct TexPixels {
    uint64_t             version = 0; // 読み戻した時点の BakePass::Version()
    int                  width = 0, height = 0;
    std::vector<uint8_t> rgba;
    int                  mode    = 0;    // 読み戻した時点のモード / 最大曲率 (表示値 → 曲率の復元に使う)
    float                maxCurv = 1.0f;
};

// UI が読み書きするアプリの状態 (描画・ファイル処理は App が担当する)。
struct AppState {
    // ---- ファイル ----
    std::string modelPath;                      // 読み込んだ(読み込み中の)モデルのパス。UTF-8
    std::string savePath = "curvature.png";     // 保存ダイアログの初期値 / 直近の保存先。UTF-8
    StatusMsg   modelStatus{Str::StNoModel};    // 言語を切り替えても追従するよう、文字列ではなく ID + 引数で持つ
    StatusMsg   saveStatus;

    // ---- ベイク対象 (モデル内のパーツ / UV チャンネルの選択) ----
    std::shared_ptr<const ModelScene> scene;       // 読み込んだ全パーツ (読み込み完了で App が設定)
    ModelSelection                    sel;         // UI で編集中の選択
    ModelSelection                    selApplied;  // 現在のベイクに反映済みの選択 (sel と違えば「選択を適用」が押せる)

    // ---- ベイク設定 ----
    int   texW = 1024, texH = 1024;
    int   mode          = 0;     // 0: 符号付き, 1: 絶対値
    int   smoothing     = 2;     // 頂点ごとの曲率を隣接頂点と平均化する回数
    int   padding       = 4;     // UV島の境界を塗り広げる幅(テクセル)。0 で無効
    bool  smoothNormals = true;  // スムース法線を再計算するか
    float maxCurv        = 4.0f; // この曲率(1/単位長)で出力が飽和する
    float defaultMaxCurv = 4.0f;

    // ---- 進行状況 (App が更新する) ----
    bool hasMesh  = false;
    bool hasBaked = false;
    // 再ベイクが必要な段階 (上流が立てば下流も実行される)
    bool dirtyGeometry  = false; // モデル / テクスチャサイズ        → 曲率の計算から全て
    bool dirtySmoothing = false; // スムージング回数                 → 平滑化 + ラスタライズ + シェーディング
    bool dirtyPadding   = false; // パディング幅                     → パディング + シェーディング
    bool dirtyShading   = false; // モード / 最大曲率                → シェーディングのみ
    bool AnyDirty() const { return dirtyGeometry || dirtySmoothing || dirtyPadding || dirtyShading; }
    bool loading  = false; // モデル読み込み/メッシュ更新ジョブが実行中
    bool saving   = false; // 画像保存ジョブが実行中

    // ---- 3Dビュー ----
    OrbitCamera camera;
    bool        lit            = true;  // ライティングの有無
    int         aaSamples      = 4;     // アンチエイリアス (MSAA のサンプル数: 1=オフ, 2, 4, 8)
    int         anisotropy     = 16;    // 異方性フィルタの倍率 (1=オフ, 2, 4, 8, 16)
    bool        view3dVisible  = false; // 今フレーム 3D タブが表示されているか (UI が設定)
    int         viewReqW = 0, viewReqH = 0; // 3D 表示領域のピクセルサイズ (UI が設定)

    // ---- テクスチャタブ ----
    TexViewState texView;
    TexPixels    texPixels;
    bool         texPixelsWanted = false; // 今フレーム、テクスチャ上にカーソルがありピクセル値が要る (UI が設定。App が読み戻す)
};

// UI からアプリへの要求 (1フレーム分)。
struct Actions {
    bool remesh    = false; // スムース法線の切り替え
    bool applySelection = false; // ベイク対象 (パーツ / UV チャンネル) の選択を適用する
    bool applySize = false;
    bool openFile  = false; // メニュー: ファイル→開く (ダイアログで選んだモデルを読み込む)
    bool saveFile  = false; // メニュー: ファイル→保存 (ダイアログで選んだ場所へ画像を保存する)
    bool exitApp   = false; // メニュー: ファイル→終了
};
