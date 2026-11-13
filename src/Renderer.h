#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "Mesh.h"

using Microsoft::WRL::ComPtr;

// DirectX12 レンダラ。
//   パス1: VS で UV を SV_POSITION として出力 → PS で 法線/位置 を UV空間テクスチャへ書き出す
//   パス2: フルスクリーン PS で 法線(と位置)から曲率を計算し、結果テクスチャへ書き出す
class Renderer {
public:
    static constexpr UINT        kBackBufferCount = 2;
    static constexpr UINT        kSrvHeapSize     = 64;
    static constexpr UINT        kSrvReserved     = 3; // 0:normal 1:position 2:result (ImGuiは3以降を使う)
    static constexpr DXGI_FORMAT kBackFormat      = DXGI_FORMAT_R8G8B8A8_UNORM;

    void Init(HWND hwnd);
    void Shutdown();
    void Resize(UINT width, UINT height);
    void WaitIdle();

    void SetMesh(const Mesh& mesh);
    void SetBakeSize(UINT width, UINT height);
    bool HasMesh() const { return indexCount_ != 0; }
    UINT BakeWidth() const { return bakeW_; }
    UINT BakeHeight() const { return bakeH_; }

    // フレーム
    void BeginFrame();
    void RecordBake(float invRange, uint32_t mode, uint32_t radius); // mode 0: 符号付き(0.5=平坦), 1: 絶対値 / radius: 差分のサンプル幅(テクセル)
    void BeginBackbuffer(float r, float g, float b);
    void EndFrame();

    // 結果テクスチャをRGBA8で読み戻す(フレーム外で呼ぶ)
    bool ReadbackResult(std::vector<uint8_t>& rgba);

    // ImGui連携
    ID3D12Device*              Device() const { return device_.Get(); }
    ID3D12CommandQueue*        Queue() const { return queue_.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return cmdList_.Get(); }
    ID3D12DescriptorHeap*      SrvHeap() const { return srvHeap_.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE ResultSrvGpu() const { return SrvGpu(2); }
    void AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    void FreeSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

private:
    D3D12_CPU_DESCRIPTOR_HANDLE RtvCpu(UINT i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(UINT i) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu(UINT i) const;

    void CreateBackBuffers();
    void CreateBakeTargets(UINT w, UINT h);
    void CreatePipelines();
    ComPtr<ID3D12Resource> CreateUploadBuffer(const void* data, UINT64 size);

    ComPtr<ID3D12Device>              device_;
    ComPtr<IDXGIFactory4>             factory_;
    ComPtr<ID3D12CommandQueue>        queue_;
    ComPtr<IDXGISwapChain3>           swapchain_;
    ComPtr<ID3D12CommandAllocator>    cmdAlloc_;
    ComPtr<ID3D12GraphicsCommandList> cmdList_;
    ComPtr<ID3D12Fence>               fence_;
    HANDLE                            fenceEvent_ = nullptr;
    UINT64                            fenceValue_ = 0;

    ComPtr<ID3D12DescriptorHeap> rtvHeap_; // [0,1]=backbuffer [2]=normal [3]=position [4]=result
    ComPtr<ID3D12DescriptorHeap> srvHeap_; // [0]=normal [1]=position [2]=result [3..]=ImGui
    UINT                         rtvInc_ = 0, srvInc_ = 0;
    std::vector<UINT>            freeSrv_;

    ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
    UINT                   frameIndex_ = 0;
    UINT                   backW_ = 0, backH_ = 0;

    // ベイク用ターゲット
    ComPtr<ID3D12Resource> normalRT_, posRT_, resultRT_;
    UINT                   bakeW_ = 1024, bakeH_ = 1024;

    // メッシュ
    ComPtr<ID3D12Resource>   vb_, ib_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW  ibv_{};
    UINT                     indexCount_ = 0;

    // パイプライン
    ComPtr<ID3D12RootSignature> rs1_, rs2_;
    ComPtr<ID3D12PipelineState> pso1_, pso2_;
};
