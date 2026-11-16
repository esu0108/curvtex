#pragma once
#include <cstdint>
#include <vector>

#include "gpu/CurvatureCompute.h"
#include "gpu/GpuMesh.h"
#include "gpu/GpuUtil.h"
#include "gpu/Renderer.h"

// 曲率テクスチャのベイク。頂点ごとの曲率(CurvatureCompute)を UV 空間へ落とし込む。
//   ラスタライズ        : 頂点の曲率を三角形内で補間しながら UV 空間のテクスチャへ書く (x=曲率, y=カバレッジ)
//   ジャンプフラッディング: モデルのあるテクセルから「最も近いテクセルの座標」を全体へ伝播させる (パディング用)
//   シェーディング      : 曲率を表示用の 0..1 に変換し、モデルの無いテクセルは最寄りのテクセルの値で埋める
class BakePass {
public:
    struct Params {
        float    invRange; // 1 / 最大曲率
        uint32_t mode;     // 0: 符号付き(0.5=平坦), 1: 絶対値
        uint32_t padding;  // UV島の外へ塗り広げる幅(テクセル)。0 で無効 (パディングの段階と同じ値を渡す)
    };

    void Init(Renderer& renderer);        // パイプライン作成 + 既定サイズのターゲット作成
    void Resize(UINT width, UINT height); // ターゲットを作り直す (GPU を待つ)

    // ---- ベイクは 3 段階。上流が変わった時だけ下流を含めて再実行する (BeginFrame の後で呼ぶ) ----
    //   ラスタライズ : モデル / テクスチャサイズ / スムージング回数が変わった時
    //   パディング   : モデル / テクスチャサイズ / パディング幅が変わった時 (ラスタライズ後に実行すること)
    //   シェーディング: 上記のどれか、または モード / 最大曲率が変わった時
    void RecordRaster(const GpuMesh& mesh, const CurvatureCompute& curvature);
    void RecordPadding(uint32_t padding);
    void RecordShading(const Params& params);

    // 結果テクスチャを RGBA8 で読み戻す (フレーム外で呼ぶ)
    bool Readback(std::vector<uint8_t>& rgba);

    // 結果テクスチャの内容が変わるたびに増える (作り直し・シェーディングの実行)。派生テクスチャ(ミップなど)の更新判定に使う。
    uint64_t Version() const { return version_; }

    UINT Width() const { return w_; }
    UINT Height() const { return h_; }
    D3D12_GPU_DESCRIPTOR_HANDLE ResultSrv() const { return r_->SrvGpu(Slot::kSrvResult); }

private:
    void CreatePipelines();
    void CreateTargets(UINT w, UINT h);

    // ジャンプフラッディングを記録し、最終結果が入った SRV スロットを返す。
    UINT RecordJumpFlood(uint32_t padding);

    Renderer* r_ = nullptr;
    UINT      w_ = 1024, h_ = 1024;

    UINT seedSlot_    = Slot::kSrvSeedA; // ジャンプフラッディングの最終結果が入っている SRV スロット
    uint64_t version_     = 1;
    bool rasterReady_ = false;           // ラスタライズ済みか (ターゲット作り直しで false に戻る)

    gpu::ComPtr<ID3D12Resource>      curvTex_, result_;
    gpu::ComPtr<ID3D12Resource>      seed_[2]; // ジャンプフラッディングのピンポン用 (R16G16_UINT: 最寄りのテクセル座標)
    gpu::ComPtr<ID3D12RootSignature> rsRaster_, rsShade_, rsJfaInit_, rsJfaStep_;
    gpu::ComPtr<ID3D12PipelineState> psoRaster_, psoShade_, psoJfaInit_, psoJfaStep_;
};
