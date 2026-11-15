#include "Camera.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace {

// 注視点→カメラの方向(単位ベクトル)
XMVECTOR EyeDirection(float yaw, float pitch) {
    return XMVectorSet(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw), 0.0f);
}

} // namespace

void OrbitCamera::Reset() {
    *this = OrbitCamera{};
}

void OrbitCamera::Orbit(float dx, float dy) {
    yaw -= dx * 0.01f;
    pitch = std::clamp(pitch + dy * 0.01f, -1.55f, 1.55f); // 真上/真下の手前で止めて反転を防ぐ
}

void OrbitCamera::Pan(float dx, float dy, float viewHeightPixels) {
    // カメラの右・上ベクトル
    const XMVECTOR fwd = XMVectorNegate(EyeDirection(yaw, pitch));
    const XMVECTOR r   = XMVector3Normalize(XMVector3Cross(fwd, XMVectorSet(0, 1, 0, 0)));
    const XMVECTOR u   = XMVector3Cross(r, fwd);
    XMFLOAT3       right, up;
    XMStoreFloat3(&right, r);
    XMStoreFloat3(&up, u);

    // 注視点での 1ピクセル あたりの距離。シーンがカーソルに追従するよう、注視点は逆向きへ動かす。
    const float k = 2.0f * dist * std::tan(kFovY * 0.5f) / std::max(viewHeightPixels, 1.0f);
    target[0] += (-right.x * dx + up.x * dy) * k;
    target[1] += (-right.y * dx + up.y * dy) * k;
    target[2] += (-right.z * dx + up.z * dy) * k;
}

void OrbitCamera::Zoom(float wheelSteps) {
    dist = std::clamp(dist * std::pow(0.9f, wheelSteps), 0.2f, 50.0f);
}

void OrbitCamera::Matrices(const Mesh& mesh, float aspect, float mvp[16], float view[16]) const {
    const float    s     = 1.0f / mesh.radius;
    const XMMATRIX model = XMMatrixTranslation(-mesh.center[0], -mesh.center[1], -mesh.center[2]) * XMMatrixScaling(s, s, s);
    const XMVECTOR tgt   = XMVectorSet(target[0], target[1], target[2], 1.0f);
    const XMVECTOR eye   = XMVectorAdd(tgt, XMVectorScale(EyeDirection(yaw, pitch), dist));
    const XMMATRIX v     = XMMatrixLookAtRH(eye, tgt, XMVectorSet(0, 1, 0, 0));
    const XMMATRIX p     = XMMatrixPerspectiveFovRH(kFovY, aspect, 0.02f, 200.0f);
    XMFLOAT4X4     fm, fv;
    XMStoreFloat4x4(&fm, model * v * p);
    XMStoreFloat4x4(&fv, v);
    std::memcpy(mvp, &fm, sizeof(fm));
    std::memcpy(view, &fv, sizeof(fv));
}
