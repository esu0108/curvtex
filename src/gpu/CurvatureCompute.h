#pragma once
#include <cstdint>

#include "gpu/GpuUtil.h"
#include "gpu/Renderer.h"
#include "mesh/CurvatureGraph.h"

// 頂点(ノード)ごとの曲率を GPU のコンピュートシェーダーで求める。
//   SetGraph      : 隣接グラフ(位置・法線・CSR)を GPU へ転送する (モデルを読み込んだ時)
//   RecordRaw     : 全ノードの曲率を計算する
//   RecordSmoothing: 生の曲率を指定回数だけ平滑化する (回数を変えても RecordRaw のやり直しは不要)
// 結果は FinalSrv() の StructuredBuffer<float> (ノードごと) で、BakePass が頂点属性として読む。
class CurvatureCompute {
public:
    void Init(Renderer& renderer); // ルートシグネチャ + コンピュート PSO

    // WaitIdle して古いバッファを破棄し、グラフを転送する。
    void SetGraph(const CurvatureGraph& graph);

    bool Ready() const { return nodeCount_ != 0; }

    // BeginFrame の後で呼ぶ
    void RecordRaw();
    void RecordSmoothing(uint32_t iterations);

    // ラスタライズ(BakePass)が読むバッファ
    D3D12_GPU_DESCRIPTOR_HANDLE NodeMapSrv() const { return r_->SrvGpu(Slot::kSrvNodeMap); }
    D3D12_GPU_DESCRIPTOR_HANDLE FinalSrv() const { return r_->SrvGpu(finalSlot_); }

private:
    // curv_[dst] へ書くディスパッチ。src < 0 なら CSCurvature (グラフのみ読む)、そうでなければ curv_[src] を入力にした CSSmooth。
    void Dispatch(int dst, int src);
    void Transition(int buf, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);

    static constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // CS / VS から読む状態
    static constexpr D3D12_RESOURCE_STATES kUav  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    Renderer* r_         = nullptr;
    uint32_t  nodeCount_ = 0;
    UINT      finalSlot_ = Slot::kSrvCurvRaw; // 最終結果が入っている SRV スロット
    bool      rawReady_  = false;

    // グラフ (読み取り専用)
    gpu::ComPtr<ID3D12Resource> positions_, normals_, offsets_, neighbors_, nodeMap_;
    // 頂点ごとの曲率: [0]=生, [1]=A, [2]=B (スムージングは 生→A→B→A→... とピンポン)
    gpu::ComPtr<ID3D12Resource> curv_[3];

    gpu::ComPtr<ID3D12RootSignature> rsCurv_, rsSmooth_;
    gpu::ComPtr<ID3D12PipelineState> psoCurv_, psoSmooth_;
};
