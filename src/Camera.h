#pragma once
#include "Mesh.h"

// オービットカメラ (Y-up, 右手系)。距離・移動量はモデルの境界球半径を 1 とした単位。
struct OrbitCamera {
    static constexpr float kFovY = 0.7853982f; // 縦画角 45度

    float yaw = 0.6f, pitch = 0.35f, dist = 3.0f;
    float target[3] = {0, 0, 0}; // 注視点(モデル中心からのオフセット)

    void Reset();
    void Orbit(float dxPixels, float dyPixels);                        // 左ドラッグ: 回転
    void Pan(float dxPixels, float dyPixels, float viewHeightPixels);  // 右/中ドラッグ: 注視点を平行移動
    void Zoom(float wheelSteps);                                       // ホイール: 距離を変える

    // モデル行列(中心を原点へ・半径1へ正規化) × ビュー × 射影 と、ビュー行列。どちらも行優先(HLSL 側は row_major)。
    void Matrices(const Mesh& mesh, float aspect, float mvp[16], float view[16]) const;
};
