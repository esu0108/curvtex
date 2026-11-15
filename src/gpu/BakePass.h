#pragma once
#include <cstdint>
#include <vector>

#include "gpu/GpuMesh.h"
#include "gpu/GpuUtil.h"
#include "gpu/Renderer.h"

// 曲率のベイク。
//   パス1: VS で UV を SV_POSITION として出力 → PS で 法線/位置 を UV空間テクスチャへ書き出す
//   (パディング有効時) ジャンプフラッディング: モデルのあるテクセルから「最も近いテクセルの座標」を全体へ伝播させる
//   パス2: フルスクリーン PS で 法線(と位置)から曲率を計算し、結果テクスチャへ書き出す。
//          モデルの無いテクセルは、ジャンプフラッディングで求めた最寄りのテクセルの値で埋める(パディング)
class BakePass {
public:
    struct Params {
        float    invRange; // 1 / 最大曲率
        uint32_t mode;     // 0: 符号付き(0.5=平坦), 1: 絶対値
        uint32_t radius;   // 差分のサンプル幅(テクセル)
        uint32_t padding;  // UV島の外へ塗り広げる幅(テクセル)。0 で無効
    };

    void Init(Renderer& renderer);          // パイプライン作成 + 既定サイズのターゲット作成
    void Resize(UINT width, UINT height);   // ターゲットを作り直す (GPU を待つ)
    void Record(const GpuMesh& mesh, const Params& params); // BeginFrame の後で呼ぶ

    // 結果テクスチャを RGBA8 で読み戻す (フレーム外で呼ぶ)
    bool Readback(std::vector<uint8_t>& rgba);

    UINT Width() const { return w_; }
    UINT Height() const { return h_; }
    D3D12_GPU_DESCRIPTOR_HANDLE ResultSrv() const { return r_->SrvGpu(Slot::kSrvResult); }

private:
    void CreatePipelines();
    void CreateTargets(UINT w, UINT h);

    // ジャンプフラッディングを記録し、最終結果が入った SRV スロットを返す。
    UINT RecordJumpFlood(uint32_t padding, const D3D12_VIEWPORT& vp, const D3D12_RECT& sc);

    Renderer* r_ = nullptr;
    UINT      w_ = 1024, h_ = 1024;

    gpu::ComPtr<ID3D12Resource>      normal_, position_, result_;
    gpu::ComPtr<ID3D12Resource>      seed_[2]; // ジャンプフラッディングのピンポン用 (R16G16_UINT: 最寄りのテクセル座標)
    gpu::ComPtr<ID3D12RootSignature> rs1_, rs2_, rsJfaInit_, rsJfaStep_;
    gpu::ComPtr<ID3D12PipelineState> pso1_, pso2_, psoJfaInit_, psoJfaStep_;
};
