#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <functional>
#include <vector>

using Microsoft::WRL::ComPtr;

// ディスクリプタヒープ内のスロット割り当て。ベイク/3Dビューの各パスとアプリ(ImGui)で共有する。
namespace Slot {
// RTV ヒープ (kRtvCount 個): 0,1 はバックバッファ
constexpr UINT kRtvNormal   = 2;
constexpr UINT kRtvPosition = 3;
constexpr UINT kRtvResult   = 4;
constexpr UINT kRtvView     = 5;
constexpr UINT kRtvSeedA    = 6; // ジャンプフラッディングのピンポン用
constexpr UINT kRtvSeedB    = 7;
constexpr UINT kRtvCount    = 8;
// SRV ヒープ: kSrvNormal と kSrvPosition は連続している必要がある(パス2が1つのテーブルで読む)。
// kSrvReserved 以降は ImGui が使う。
constexpr UINT kSrvNormal   = 0;
constexpr UINT kSrvPosition = 1;
constexpr UINT kSrvResult   = 2;
constexpr UINT kSrvView     = 3;
constexpr UINT kSrvSeedA    = 4;
constexpr UINT kSrvSeedB    = 5;
constexpr UINT kSrvReserved = 6;
} // namespace Slot

// DirectX12 の土台: デバイス / キュー / スワップチェーン / ディスクリプタヒープ / フェンス / コマンドリスト。
// 描画内容は持たず、ベイクは BakePass、3Dビューは ViewPass、メッシュは GpuMesh が担当する。
// 1フレームの流れ:  BeginFrame → (各パスの Record) → BeginBackbuffer → (ImGui 描画) → EndFrame
class Renderer {
public:
    static constexpr UINT        kBackBufferCount = 2;
    static constexpr UINT        kSrvHeapSize     = 64;
    static constexpr DXGI_FORMAT kBackFormat      = DXGI_FORMAT_R8G8B8A8_UNORM;

    void Init(HWND hwnd);
    void Shutdown();
    void Resize(UINT width, UINT height);
    void WaitIdle();

    // フレーム
    void BeginFrame();
    void BeginBackbuffer(float r, float g, float b);
    void EndFrame();

    // フレーム外で単発のコマンドを実行して完了まで待つ(読み戻しなど)。
    void ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record);

    ID3D12Device*              Device() const { return device_.Get(); }
    ID3D12CommandQueue*        Queue() const { return queue_.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return cmdList_.Get(); }
    ID3D12DescriptorHeap*      SrvHeap() const { return srvHeap_.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE RtvCpu(UINT i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(UINT i) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu(UINT i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE DsvCpu() const; // 3Dビュー用の深度(1個)

    // ImGui の DX12 バックエンドへ渡す SRV アロケータ (kSrvReserved 以降を払い出す)
    void AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    void FreeSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

private:
    void CreateBackBuffers();

    ComPtr<ID3D12Device>              device_;
    ComPtr<IDXGIFactory4>             factory_;
    ComPtr<ID3D12CommandQueue>        queue_;
    ComPtr<IDXGISwapChain3>           swapchain_;
    ComPtr<ID3D12CommandAllocator>    cmdAlloc_;
    ComPtr<ID3D12GraphicsCommandList> cmdList_;
    ComPtr<ID3D12Fence>               fence_;
    HANDLE                            fenceEvent_ = nullptr;
    UINT64                            fenceValue_ = 0;

    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT                         rtvInc_ = 0, srvInc_ = 0;
    std::vector<UINT>            freeSrv_;

    ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
    UINT                   frameIndex_ = 0;
    UINT                   backW_ = 0, backH_ = 0;
};
