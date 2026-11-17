#pragma once
#include "gpu/GpuUtil.h"

struct ImDrawData;
struct ImDrawList;
struct ImDrawCmd;

// ImGui の DX12 バックエンドは、サンプラーが線形補間のものに固定されている。
// 画像を最近傍(補間なし)で描きたいときは、DrawList の中で次のように挟む:
//
//   dl->AddCallback(UiPointSampler::Begin, nullptr);          // 点サンプラー版の ImGui 用パイプラインへ切り替え
//   dl->AddImage(...);
//   dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr); // バックエンドの標準の状態へ戻す
//
// ImGui_ImplDX12_RenderDrawData の直前に BeginFrame を呼んでおくこと。
class UiPointSampler {
public:
    static UiPointSampler& Instance();

    // 作れなかった場合は例外を投げる。その場合 Ready() は false のままで、呼び出し側は線形補間で描く。
    void Init(ID3D12Device* dev, DXGI_FORMAT rtvFormat);
    void Shutdown();
    bool Ready() const { return pso_ != nullptr; }

    // 今フレームの ImGui 描画に使うコマンドリストと投影行列を覚える
    void BeginFrame(ID3D12GraphicsCommandList* cl, const ImDrawData* drawData);

    // ImDrawCallback。cl_ が設定されていなければ何もしない。
    static void Begin(const ImDrawList* parent, const ImDrawCmd* cmd);

private:
    gpu::ComPtr<ID3D12RootSignature> rs_;
    gpu::ComPtr<ID3D12PipelineState> pso_;
    ID3D12GraphicsCommandList*       cl_ = nullptr;
    float                            mvp_[4][4] = {};
};
