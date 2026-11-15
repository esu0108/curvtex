#pragma once
#include "BakePass.h"
#include "GpuMesh.h"
#include "GpuUtil.h"
#include "Renderer.h"

// 3Dビュー: 曲率テクスチャ(BakePass の結果)を貼ったモデルをオフスクリーンへ描く。
// 描いた結果は ImGui::Image で表示する。
class ViewPass {
public:
    void Init(Renderer& renderer);
    void Resize(UINT width, UINT height); // サイズが変わった時だけ作り直す。フレーム記録前(BeginFrame の前)に呼ぶ

    // mvp / view は行優先 4x4 (DirectXMath の XMFLOAT4X4 と同じ並び)。BakePass::Record の後で呼ぶこと。
    void Record(const GpuMesh& mesh, const BakePass& bake, const float* mvp16, const float* view16, bool lit);

    UINT Width() const { return w_; }
    UINT Height() const { return h_; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return r_->SrvGpu(Slot::kSrvView); }

private:
    void CreatePipeline();
    void CreateTargets(UINT w, UINT h);

    Renderer* r_ = nullptr;
    UINT      w_ = 64, h_ = 64;

    gpu::ComPtr<ID3D12Resource>      color_, depth_;
    gpu::ComPtr<ID3D12RootSignature> rs_;
    gpu::ComPtr<ID3D12PipelineState> pso_;
};
