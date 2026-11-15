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
        uint32_t padding;  // UV島の外へ塗り広げる幅(テクセル)。0 で無効 (パディングの段階と同じ値を渡す)
    };

    void Init(Renderer& renderer);          // パイプライン作成 + 既定サイズのターゲット作成
    void Resize(UINT width, UINT height);   // ターゲットを作り直す (GPU を待つ)

    // ---- ベイクは 3 段階。上流が変わった時だけ下流を含めて再実行する (BeginFrame の後で呼ぶ) ----
    //   ジオメトリ  : モデル・テクスチャサイズが変わった時            → パス1 (法線/位置)
    //   パディング  : 上記 または パディング幅が変わった時           → ジャンプフラッディング
    //   シェーディング: 上記 または モード/サンプル幅/最大曲率が変わった時 → パス2 (曲率)
    void RecordGeometry(const GpuMesh& mesh);
    void RecordPadding(uint32_t padding);
    void RecordShading(const Params& params);

    // 結果テクスチャを RGBA8 で読み戻す (フレーム外で呼ぶ)
    bool Readback(std::vector<uint8_t>& rgba);

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

    // パス1 で書いた位置は 中心・半径で正規化済み。曲率を元の単位へ戻すために半径の逆数を覚えておく。
    float invMeshRadius_ = 1.0f;
    UINT  seedSlot_      = Slot::kSrvSeedA; // ジャンプフラッディングの最終結果が入っている SRV スロット
    bool  geometryReady_ = false;           // パス1 の結果が有効か (ターゲット作り直しで false に戻る)

    gpu::ComPtr<ID3D12Resource>      normal_, position_, result_;
    gpu::ComPtr<ID3D12Resource>      seed_[2]; // ジャンプフラッディングのピンポン用 (R16G16_UINT: 最寄りのテクセル座標)
    gpu::ComPtr<ID3D12RootSignature> rs1_, rs2_, rsJfaInit_, rsJfaStep_;
    gpu::ComPtr<ID3D12PipelineState> pso1_, pso2_, psoJfaInit_, psoJfaStep_;
};
