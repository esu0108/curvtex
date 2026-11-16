#pragma once
#include "gpu/BakePass.h"
#include "gpu/GpuMesh.h"
#include "gpu/GpuUtil.h"
#include "gpu/Renderer.h"

// 3Dビュー: 曲率テクスチャ(BakePass の結果)を貼ったモデルをオフスクリーンへ描く。
// 描いた結果は ImGui::Image で表示する。
class ViewPass {
public:
    void Init(Renderer& renderer);
    void Resize(UINT width, UINT height); // サイズが変わった時だけ作り直す。フレーム記録前(BeginFrame の前)に呼ぶ

    // 曲率テクスチャ(BakePass の結果)と同じサイズの、ミップ付きテクスチャを用意する。サイズが変わった時だけ作り直す。
    // フレーム記録前(BeginFrame の前)に呼ぶ。ミップの中身は Record が、曲率テクスチャが更新された時にだけ作り直す。
    void PrepareTexture(UINT texWidth, UINT texHeight);

    // 描画品質。変わった時だけ作り直す。フレーム記録前(BeginFrame の前)に呼ぶ。
    //   msaaSamples : アンチエイリアスのサンプル数 (1=オフ)。デバイスが対応していない値は、対応する最大の値に丸める。
    //   anisotropy  : 異方性フィルタの倍率 (1=オフ, 最大16)
    void SetQuality(UINT msaaSamples, UINT anisotropy);
    bool SupportsMsaa(UINT samples) const { return samples == 1 || (samples < 32 && (msaaMask_ >> samples & 1u)); }

    // mvp / view は行優先 4x4 (DirectXMath の XMFLOAT4X4 と同じ並び)。BakePass::Record の後で呼ぶこと。
    void Record(const GpuMesh& mesh, const BakePass& bake, const float* mvp16, const float* view16, bool lit);

    UINT Width() const { return w_; }
    UINT Height() const { return h_; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return r_->SrvGpu(Slot::kSrvView); }

private:
    void CreateMipPipeline();
    void CreateMipTexture(UINT w, UINT h);
    void RecordMips(const BakePass& bake); // 曲率テクスチャが変わっていたら、ミップを作り直す

    void CreatePipeline(); // ルートシグネチャ(異方性フィルタのサンプラーを含む) と PSO (サンプル数を含む)
    void CreateTargets(UINT w, UINT h);

    Renderer* r_ = nullptr;
    UINT      w_ = 64, h_ = 64;
    UINT      samples_  = 1; // 現在の MSAA サンプル数
    UINT      aniso_    = 1; // 現在の異方性フィルタ倍率
    UINT      msaaMask_ = 0; // 対応しているサンプル数のビットマスク (bit n = nx に対応)

    // ミップ付きの曲率テクスチャ。BakePass の結果をレベル 0 として描き写し、縮小しながら下のレベルを作る。
    gpu::ComPtr<ID3D12Resource>      mipTex_;
    UINT                             mipW_ = 0, mipH_ = 0, mipLevels_ = 0;
    uint64_t                         mipVersion_ = 0; // 最後にミップを作った時の BakePass::Version()
    gpu::ComPtr<ID3D12RootSignature> rsMip_;
    gpu::ComPtr<ID3D12PipelineState> psoMip_;

    gpu::ComPtr<ID3D12Resource>      color_;     // 表示用 (1サンプル)。MSAA 時は msaaColor_ の解決結果
    gpu::ComPtr<ID3D12Resource>      msaaColor_; // MSAA 時だけ作る描画先
    gpu::ComPtr<ID3D12Resource>      depth_;
    gpu::ComPtr<ID3D12RootSignature> rs_;
    gpu::ComPtr<ID3D12PipelineState> pso_;
};
