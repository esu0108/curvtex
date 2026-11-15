#pragma once
#include <string>

#include "ui/Camera.h"

// UI が読み書きするアプリの状態 (描画・ファイル処理は App が担当する)。
struct AppState {
    // ---- ファイル ----
    std::string modelPath;                      // UTF-8
    std::string savePath = "curvature.png";     // UTF-8
    std::string modelStatus = "モデル未読み込み";
    std::string saveStatus;

    // ---- ベイク設定 ----
    int   texW = 1024, texH = 1024;
    int   mode          = 0;     // 0: 符号付き, 1: 絶対値
    int   sampleRadius  = 3;     // 曲率差分のサンプル幅(テクセル)
    int   padding       = 4;     // UV島の境界を塗り広げる幅(テクセル)。0 で無効
    bool  smoothNormals = true;  // スムース法線を再計算するか
    float maxCurv        = 4.0f; // この曲率(1/単位長)で出力が飽和する
    float defaultMaxCurv = 4.0f;

    // ---- 進行状況 (App が更新する) ----
    bool hasMesh  = false;
    bool hasBaked = false;
    bool dirty    = false; // true なら次のフレームで再ベイクする
    bool loading  = false; // モデル読み込み/メッシュ更新ジョブが実行中
    bool saving   = false; // 画像保存ジョブが実行中

    // ---- 3Dビュー ----
    OrbitCamera camera;
    bool        lit            = true;  // ライティングの有無
    bool        view3dVisible  = false; // 今フレーム 3D タブが表示されているか (UI が設定)
    int         viewReqW = 0, viewReqH = 0; // 3D 表示領域のピクセルサイズ (UI が設定)
};

// UI からアプリへの要求 (1フレーム分)。
struct Actions {
    bool remesh = false;     // スムース法線の切り替え
    bool browseModel = false;
    bool load = false;
    bool applySize = false;
    bool browseSave = false;
    bool save = false;
};
