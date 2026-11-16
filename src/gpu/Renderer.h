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
constexpr UINT kMaxMipLevels = 13; // 曲率テクスチャは最大 4096 なので、4096 → 1 の 13 レベル
// RTV ヒープ (kRtvCount 個): 0,1 はバックバッファ
constexpr UINT kRtvCurv   = 2; // 曲率(UV空間)
constexpr UINT kRtvResult = 3; // 表示用の結果
constexpr UINT kRtvView   = 4; // 3Dビュー
constexpr UINT kRtvSeedA  = 5; // ジャンプフラッディングのピンポン用
constexpr UINT kRtvSeedB  = 6;
constexpr UINT kRtvViewMsaa = 7; // 3Dビューのマルチサンプル描画先
constexpr UINT kRtvMip    = 8; // 8.. = ミップ生成の描画先 (レベルごと。kMaxMipLevels 個)
constexpr UINT kRtvCount  = kRtvMip + kMaxMipLevels;
// SRV/UAV ヒープ。kSrvReserved 以降は ImGui が使う。
constexpr UINT kSrvCurvTex = 0; // 曲率(UV空間)
constexpr UINT kSrvResult  = 1;
constexpr UINT kSrvView    = 2;
constexpr UINT kSrvSeedA   = 3;
constexpr UINT kSrvSeedB   = 4;
// 曲率の計算 (CurvatureCompute)。kSrvGraph からの 4 つは連続している必要がある(1つのテーブルで読む)。
constexpr UINT kSrvGraph    = 5; // 5..8 = 位置, 法線, offsets, neighbors
constexpr UINT kSrvNodeMap  = 9; // 頂点 → ノード
constexpr UINT kSrvCurvRaw  = 10; // 頂点ごとの曲率: 生 / スムージングのピンポン A, B
constexpr UINT kSrvCurvA    = 11;
constexpr UINT kSrvCurvB    = 12;
constexpr UINT kUavCurvRaw  = 13;
constexpr UINT kUavCurvA    = 14;
constexpr UINT kUavCurvB    = 15;
constexpr UINT kSrvViewTex  = 16; // 3Dビューが貼る曲率テクスチャ (全ミップ)
constexpr UINT kSrvMipSrc   = 17; // 17.. = ミップ生成の入力用に 1 レベルずつ見る SRV (kMaxMipLevels - 1 個)
constexpr UINT kSrvReserved = kSrvMipSrc + kMaxMipLevels - 1;
} // namespace Slot

// DirectX12 の土台: デバイス / キュー / スワップチェーン / ディスクリプタヒープ / フェンス / コマンドリスト。
// 描画内容は持たず、ベイクは BakePass、3Dビューは ViewPass、メッシュは GpuMesh が担当する。
// 1フレームの流れ:  BeginFrame → (各パスの Record) → BeginBackbuffer → (ImGui 描画) → EndFrame
// フレームは kBackBufferCount 枚まで同時に GPU へ投げられる(CPU は GPU の2フレーム先まで進める)。
// リソースの作り直しや読み戻しなど、GPU の使用中に触れない操作の前には WaitIdle() を呼ぶこと。
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
    void WaitForFence(UINT64 value); // fence が value に達するまで待つ

    ComPtr<ID3D12Device>              device_;
    ComPtr<IDXGIFactory4>             factory_;
    ComPtr<ID3D12CommandQueue>        queue_;
    ComPtr<IDXGISwapChain3>           swapchain_;
    ComPtr<ID3D12CommandAllocator>    cmdAlloc_[kBackBufferCount]; // フレームごと (GPU が使い終わるまで Reset できない)
    ComPtr<ID3D12GraphicsCommandList> cmdList_;
    ComPtr<ID3D12Fence>               fence_;
    HANDLE                            fenceEvent_ = nullptr;
    UINT64                            fenceValue_ = 0;                // 最後に Signal した値
    UINT64                            frameFence_[kBackBufferCount] = {}; // そのフレームの完了を表す fence 値
    UINT                              frameSlot_ = 0;                 // 次に記録するフレームのスロット

    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT                         rtvInc_ = 0, srvInc_ = 0;
    std::vector<UINT>            freeSrv_;

    ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];
    UINT                   frameIndex_ = 0;
    UINT                   backW_ = 0, backH_ = 0;
};
