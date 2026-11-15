#include "gpu/Renderer.h"

#include <stdexcept>
#include <vector>

#include "gpu/GpuUtil.h"

using namespace gpu;

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::RtvCpu(UINT i) const {
    auto h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(i) * rtvInc_;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::SrvCpu(UINT i) const {
    auto h = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(i) * srvInc_;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE Renderer::SrvGpu(UINT i) const {
    auto h = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(i) * srvInc_;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::DsvCpu() const {
    return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
}

void Renderer::Init(HWND hwnd) {
    UINT dxgiFlags = 0;
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    Check(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount      = kBackBufferCount;
    sd.Format           = kBackFormat;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> sc1;
    Check(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd, nullptr, nullptr, &sc1), "CreateSwapChainForHwnd");
    Check(sc1.As(&swapchain_), "QueryInterface(IDXGISwapChain3)");
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // ディスクリプタヒープ
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = Slot::kRtvCount;
    Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap_)), "CreateDescriptorHeap(RTV)");
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap_)), "CreateDescriptorHeap(DSV)");
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kSrvHeapSize;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap_)), "CreateDescriptorHeap(SRV)");
    rtvInc_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvInc_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = kSrvHeapSize; i-- > Slot::kSrvReserved;) freeSrv_.push_back(i);

    // コマンド関連
    for (auto& a : cmdAlloc_) Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)), "CreateCommandAllocator");
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc_[0].Get(), nullptr, IID_PPV_ARGS(&cmdList_)), "CreateCommandList");
    cmdList_->Close();
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    CreateBackBuffers();
}

void Renderer::Shutdown() {
    if (!device_) return;
    WaitIdle();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void Renderer::WaitForFence(UINT64 value) {
    if (fence_->GetCompletedValue() >= value) return;
    Check(fence_->SetEventOnCompletion(value, fenceEvent_), "SetEventOnCompletion");
    WaitForSingleObject(fenceEvent_, INFINITE);
}

// キューに積んだ全ての仕事(進行中のフレームを含む)の完了を待つ
void Renderer::WaitIdle() {
    if (!queue_ || !fence_) return;
    const UINT64 v = ++fenceValue_;
    Check(queue_->Signal(fence_.Get(), v), "Signal");
    WaitForFence(v);
}

void Renderer::CreateBackBuffers() {
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        Check(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])), "GetBuffer");
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, RtvCpu(i));
    }
    DXGI_SWAP_CHAIN_DESC1 d{};
    swapchain_->GetDesc1(&d);
    backW_ = d.Width;
    backH_ = d.Height;
}

void Renderer::Resize(UINT width, UINT height) {
    if (!swapchain_ || width == 0 || height == 0) return;
    WaitIdle();
    for (auto& b : backBuffers_) b.Reset();
    Check(swapchain_->ResizeBuffers(kBackBufferCount, width, height, kBackFormat, 0), "ResizeBuffers");
    CreateBackBuffers();
}

// ----------------------------------------------------------------------------
// フレーム
// ----------------------------------------------------------------------------
void Renderer::BeginFrame() {
    // このスロットのアロケータを最後に使ったフレーム(2フレーム前)が終わるまで待つ。
    // ImGui の DX12 バックエンドも「フレームごとのバッファ」を同じ周期で使い回すので、ここで同期が取れる。
    WaitForFence(frameFence_[frameSlot_]);
    Check(cmdAlloc_[frameSlot_]->Reset(), "CommandAllocator::Reset");
    Check(cmdList_->Reset(cmdAlloc_[frameSlot_].Get(), nullptr), "CommandList::Reset");
}

void Renderer::BeginBackbuffer(float r, float g, float b) {
    frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
    auto* cl    = cmdList_.Get();

    D3D12_RESOURCE_BARRIER bar = Transition(backBuffers_[frameIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &bar);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv    = RtvCpu(frameIndex_);
    const float                 col[4] = {r, g, b, 1.f};
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->ClearRenderTargetView(rtv, col, 0, nullptr);

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(backW_), static_cast<float>(backH_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(backW_), static_cast<LONG>(backH_)};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    cl->SetDescriptorHeaps(1, heaps);
}

void Renderer::EndFrame() {
    D3D12_RESOURCE_BARRIER bar = Transition(backBuffers_[frameIndex_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList_->ResourceBarrier(1, &bar);
    Check(cmdList_->Close(), "CommandList::Close");

    ID3D12CommandList* lists[] = {cmdList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Check(swapchain_->Present(1, 0), "Present");

    // このフレームの完了を fence で追跡する。待つのは次に同じスロットを使う時(BeginFrame)。
    frameFence_[frameSlot_] = ++fenceValue_;
    Check(queue_->Signal(fence_.Get(), frameFence_[frameSlot_]), "Signal");
    frameSlot_ = (frameSlot_ + 1) % kBackBufferCount;
}

void Renderer::ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
    WaitIdle(); // 全アロケータが空になるので、次に使うスロットのものを安全に Reset できる
    Check(cmdAlloc_[frameSlot_]->Reset(), "CommandAllocator::Reset");
    Check(cmdList_->Reset(cmdAlloc_[frameSlot_].Get(), nullptr), "CommandList::Reset");
    record(cmdList_.Get());
    Check(cmdList_->Close(), "CommandList::Close");
    ID3D12CommandList* lists[] = {cmdList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitIdle();
}

// ----------------------------------------------------------------------------
// ImGui 用 SRV アロケータ
// ----------------------------------------------------------------------------
void Renderer::AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    if (freeSrv_.empty()) throw std::runtime_error("SRV descriptor heap exhausted");
    const UINT i = freeSrv_.back();
    freeSrv_.pop_back();
    *cpu = SrvCpu(i);
    *gpu = SrvGpu(i);
}

void Renderer::FreeSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    const auto start = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    freeSrv_.push_back(static_cast<UINT>((cpu.ptr - start.ptr) / srvInc_));
}
