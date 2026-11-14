#include "Renderer.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>

// ビルド時に fxc でコンパイルされたシェーダ (shaders/*.hlsl → CMake が生成するヘッダ)
#include "pass1_vs.h"
#include "pass1_ps.h"
#include "pass2_vs.h"
#include "pass2_ps.h"
#include "view_vs.h"
#include "view_ps.h"

namespace {

// 3Dビューのルート定数: mvp(16) + view(16) + lit(1) + パディング(3)
constexpr UINT kViewConstants = 36;

// ============================================================================
// helpers
// ============================================================================
void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s failed (0x%08X)", what, static_cast<unsigned>(hr));
        throw std::runtime_error(buf);
    }
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter  = to;
    return b;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width            = size;
    d.Height           = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels        = 1;
    d.Format           = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

D3D12_RASTERIZER_DESC DefaultRaster() {
    D3D12_RASTERIZER_DESC r{};
    r.FillMode              = D3D12_FILL_MODE_SOLID;
    r.CullMode              = D3D12_CULL_MODE_NONE; // 裏表どちらも書く
    r.FrontCounterClockwise = FALSE;
    r.DepthClipEnable       = TRUE;
    r.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return r;
}

ComPtr<ID3D12RootSignature> MakeRootSignature(ID3D12Device* dev, const D3D12_ROOT_SIGNATURE_DESC& desc) {
    ComPtr<ID3DBlob> blob, err;
    HRESULT          hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &err);
    if (FAILED(hr)) {
        throw std::runtime_error(std::string("RootSignature: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : ""));
    }
    ComPtr<ID3D12RootSignature> rs;
    Check(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs)), "CreateRootSignature");
    return rs;
}

} // namespace

// ============================================================================
// Renderer
// ============================================================================
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
    hd.NumDescriptors = 8;
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
    for (UINT i = kSrvHeapSize; i-- > kSrvReserved;) freeSrv_.push_back(i);

    // コマンド関連
    Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc_)), "CreateCommandAllocator");
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc_.Get(), nullptr, IID_PPV_ARGS(&cmdList_)), "CreateCommandList");
    cmdList_->Close();
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    CreateBackBuffers();
    CreatePipelines();
    CreateBakeTargets(bakeW_, bakeH_);
    CreateViewTargets(viewW_, viewH_);
}

void Renderer::Shutdown() {
    if (!device_) return;
    WaitIdle();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void Renderer::WaitIdle() {
    if (!queue_ || !fence_) return;
    const UINT64 v = ++fenceValue_;
    Check(queue_->Signal(fence_.Get(), v), "Signal");
    if (fence_->GetCompletedValue() < v) {
        Check(fence_->SetEventOnCompletion(v, fenceEvent_), "SetEventOnCompletion");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
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

void Renderer::CreateBakeTargets(UINT w, UINT h) {
    WaitIdle();
    normalRT_.Reset();
    posRT_.Reset();
    resultRT_.Reset();
    bakeW_ = w;
    bakeH_ = h;

    auto make = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res, UINT rtvIndex, UINT srvIndex) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = fmt;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = fmt;
        Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&res)),
              "CreateCommittedResource(bake target)");
        device_->CreateRenderTargetView(res.Get(), nullptr, RtvCpu(rtvIndex));
        device_->CreateShaderResourceView(res.Get(), nullptr, SrvCpu(srvIndex));
    };
    make(DXGI_FORMAT_R32G32B32A32_FLOAT, normalRT_, 2, 0);
    make(DXGI_FORMAT_R32G32B32A32_FLOAT, posRT_, 3, 1);
    make(DXGI_FORMAT_R8G8B8A8_UNORM, resultRT_, 4, 2);
}

void Renderer::CreateViewTargets(UINT w, UINT h) {
    WaitIdle();
    viewRT_.Reset();
    viewDepth_.Reset();
    viewW_ = w;
    viewH_ = h;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // カラー (ImGui::Image で表示するので SRV も作る)
    rd.Format = kBackFormat;
    rd.Flags  = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format   = kBackFormat;
    cv.Color[0] = 0.16f;
    cv.Color[1] = 0.16f;
    cv.Color[2] = 0.19f;
    cv.Color[3] = 1.0f;
    Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&viewRT_)),
          "CreateCommittedResource(view color)");
    device_->CreateRenderTargetView(viewRT_.Get(), nullptr, RtvCpu(5));
    device_->CreateShaderResourceView(viewRT_.Get(), nullptr, SrvCpu(3));

    // 深度
    rd.Format = DXGI_FORMAT_D32_FLOAT;
    rd.Flags  = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format             = DXGI_FORMAT_D32_FLOAT;
    dcv.DepthStencil.Depth = 1.0f;
    Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&viewDepth_)),
          "CreateCommittedResource(view depth)");
    device_->CreateDepthStencilView(viewDepth_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Renderer::ResizeView(UINT w, UINT h) {
    w = std::clamp<UINT>(w, 16, 8192);
    h = std::clamp<UINT>(h, 16, 8192);
    if (w == viewW_ && h == viewH_) return;
    CreateViewTargets(w, h);
}

void Renderer::SetBakeSize(UINT w, UINT h) {
    if (w == 0 || h == 0) return;
    CreateBakeTargets(w, h);
}

void Renderer::CreatePipelines() {
    // ---- パス1 ---------------------------------------------------------
    {
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rs1_      = MakeRootSignature(device_.Get(), rsd);

        
        const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, px), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, nx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rs1_.Get();
        pd.VS             = {g_Pass1VS, sizeof(g_Pass1VS)};
        pd.PS             = {g_Pass1PS, sizeof(g_Pass1PS)};
        for (int i = 0; i < 2; ++i) pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.InputLayout           = {layout, static_cast<UINT>(std::size(layout))};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 2;
        pd.RTVFormats[0]         = DXGI_FORMAT_R32G32B32A32_FLOAT;
        pd.RTVFormats[1]         = DXGI_FORMAT_R32G32B32A32_FLOAT;
        pd.SampleDesc.Count      = 1;
        Check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso1_)), "CreateGraphicsPipelineState(pass1)");
    }

    // ---- パス2 ---------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors                    = 2;
        range.BaseShaderRegister                = 0;
        range.RegisterSpace                     = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.Num32BitValues = 6;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters   = params;
        rs2_              = MakeRootSignature(device_.Get(), rsd);

        
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rs2_.Get();
        pd.VS             = {g_Pass2VS, sizeof(g_Pass2VS)};
        pd.PS             = {g_Pass2PS, sizeof(g_Pass2PS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count      = 1;
        Check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso2_)), "CreateGraphicsPipelineState(pass2)");
    }

    // ---- 3Dビュー --------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors                    = 1;
        range.BaseShaderRegister                = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = kViewConstants;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderRegister   = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 2;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers   = &samp;
        rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rs3_                  = MakeRootSignature(device_.Get(), rsd);

        const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, px), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, nx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rs3_.Get();
        pd.VS             = {g_ViewVS, sizeof(g_ViewVS)};
        pd.PS             = {g_ViewPS, sizeof(g_ViewPS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.RasterizerState.FrontCounterClockwise = TRUE; // 右手系・反時計回りが表(SV_IsFrontFace 用)。両面描画なのでカリングはしない
        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
        pd.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;
        pd.InputLayout           = {layout, static_cast<UINT>(std::size(layout))};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kBackFormat;
        pd.SampleDesc.Count      = 1;
        Check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso3_)), "CreateGraphicsPipelineState(view)");
    }
}

ComPtr<ID3D12Resource> Renderer::CreateUploadBuffer(const void* data, UINT64 size) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC    rd = BufferDesc(size);
    ComPtr<ID3D12Resource> res;
    Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)),
          "CreateCommittedResource(upload)");
    void*         p = nullptr;
    D3D12_RANGE   r{0, 0};
    Check(res->Map(0, &r, &p), "Map");
    std::memcpy(p, data, static_cast<size_t>(size));
    res->Unmap(0, nullptr);
    return res;
}

void Renderer::SetMesh(const Mesh& mesh) {
    WaitIdle();
    vb_.Reset();
    ib_.Reset();
    indexCount_ = 0;

    const UINT64 vbSize = mesh.vertices.size() * sizeof(Vertex);
    const UINT64 ibSize = mesh.indices.size() * sizeof(uint32_t);
    vb_                 = CreateUploadBuffer(mesh.vertices.data(), vbSize);
    ib_                 = CreateUploadBuffer(mesh.indices.data(), ibSize);

    vbv_.BufferLocation = vb_->GetGPUVirtualAddress();
    vbv_.SizeInBytes    = static_cast<UINT>(vbSize);
    vbv_.StrideInBytes  = sizeof(Vertex);
    ibv_.BufferLocation = ib_->GetGPUVirtualAddress();
    ibv_.SizeInBytes    = static_cast<UINT>(ibSize);
    ibv_.Format         = DXGI_FORMAT_R32_UINT;
    indexCount_         = static_cast<UINT>(mesh.indices.size());
}

// ----------------------------------------------------------------------------
// フレーム
// ----------------------------------------------------------------------------
void Renderer::BeginFrame() {
    Check(cmdAlloc_->Reset(), "CommandAllocator::Reset");
    Check(cmdList_->Reset(cmdAlloc_.Get(), nullptr), "CommandList::Reset");
}

void Renderer::RecordBake(float invRange, uint32_t mode, uint32_t radius, uint32_t padding) {
    if (!HasMesh()) return;
    auto* cl = cmdList_.Get();

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(bakeW_), static_cast<float>(bakeH_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(bakeW_), static_cast<LONG>(bakeH_)};
    const float          clear[4] = {0.f, 0.f, 0.f, 0.f};

    // ---- パス1: 法線 + 位置 を UV空間へ ----
    {
        D3D12_RESOURCE_BARRIER b[2] = {
            Transition(normalRT_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(posRT_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };
        cl->ResourceBarrier(2, b);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {RtvCpu(2), RtvCpu(3)};
        cl->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
        cl->ClearRenderTargetView(rtvs[0], clear, 0, nullptr);
        cl->ClearRenderTargetView(rtvs[1], clear, 0, nullptr);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);
        cl->SetGraphicsRootSignature(rs1_.Get());
        cl->SetPipelineState(pso1_.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->IASetVertexBuffers(0, 1, &vbv_);
        cl->IASetIndexBuffer(&ibv_);
        cl->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);

        D3D12_RESOURCE_BARRIER back[3] = {
            Transition(normalRT_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(posRT_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(resultRT_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };
        cl->ResourceBarrier(3, back);
    }

    // ---- パス2: 法線から曲率 ----
    {
        struct {
            uint32_t w, h;
            float    invRange;
            uint32_t mode;
            uint32_t radius;
            uint32_t pad;
        } cb{bakeW_, bakeH_, invRange, mode, radius, padding};

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvCpu(4);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);

        ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetGraphicsRootSignature(rs2_.Get());
        cl->SetPipelineState(pso2_.Get());
        cl->SetGraphicsRoot32BitConstants(0, 6, &cb, 0);
        cl->SetGraphicsRootDescriptorTable(1, SrvGpu(0)); // t0=normal, t1=position
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER b = Transition(resultRT_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &b);
    }
}

void Renderer::RecordView(const float* mvp16, const float* view16, bool lit) {
    if (!HasMesh() || !viewRT_) return;
    auto* cl = cmdList_.Get();

    D3D12_RESOURCE_BARRIER toRt = Transition(viewRT_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &toRt);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvCpu(5);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    const float                       clear[4] = {0.16f, 0.16f, 0.19f, 1.0f};
    cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(viewW_), static_cast<float>(viewH_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(viewW_), static_cast<LONG>(viewH_)};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    float cb[kViewConstants] = {};
    std::memcpy(cb, mvp16, 16 * sizeof(float));
    std::memcpy(cb + 16, view16, 16 * sizeof(float));
    cb[32] = lit ? 1.0f : 0.0f;

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rs3_.Get());
    cl->SetPipelineState(pso3_.Get());
    cl->SetGraphicsRoot32BitConstants(0, kViewConstants, cb, 0);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpu(2)); // t0 = 曲率テクスチャ(結果)
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->IASetVertexBuffers(0, 1, &vbv_);
    cl->IASetIndexBuffer(&ibv_);
    cl->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);

    D3D12_RESOURCE_BARRIER toSrv = Transition(viewRT_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &toSrv);
}

void Renderer::BeginBackbuffer(float r, float g, float b) {
    frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
    auto* cl    = cmdList_.Get();

    D3D12_RESOURCE_BARRIER bar = Transition(backBuffers_[frameIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &bar);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvCpu(frameIndex_);
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
    WaitIdle(); // ツール用途なので毎フレーム完了待ち(シンプルさ優先)
}

// ----------------------------------------------------------------------------
// 読み戻し
// ----------------------------------------------------------------------------
bool Renderer::ReadbackResult(std::vector<uint8_t>& rgba) {
    if (!resultRT_) return false;
    WaitIdle();

    const D3D12_RESOURCE_DESC           rd = resultRT_->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT  fp{};
    UINT                                rows     = 0;
    UINT64                              rowSize  = 0, total = 0;
    device_->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSize, &total);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC    bd = BufferDesc(total);
    ComPtr<ID3D12Resource> rb;
    Check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)),
          "CreateCommittedResource(readback)");

    Check(cmdAlloc_->Reset(), "CommandAllocator::Reset");
    Check(cmdList_->Reset(cmdAlloc_.Get(), nullptr), "CommandList::Reset");

    D3D12_RESOURCE_BARRIER b1 = Transition(resultRT_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList_->ResourceBarrier(1, &b1);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource       = rb.Get();
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = resultRT_.Get();
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cmdList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER b2 = Transition(resultRT_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList_->ResourceBarrier(1, &b2);
    Check(cmdList_->Close(), "CommandList::Close");

    ID3D12CommandList* lists[] = {cmdList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitIdle();

    uint8_t*    mapped = nullptr;
    D3D12_RANGE rr{0, static_cast<SIZE_T>(total)};
    Check(rb->Map(0, &rr, reinterpret_cast<void**>(&mapped)), "Map(readback)");
    const UINT w = static_cast<UINT>(rd.Width), h = rd.Height;
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (UINT y = 0; y < h; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4,
                    mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                    static_cast<size_t>(w) * 4);
    }
    D3D12_RANGE wr{0, 0};
    rb->Unmap(0, &wr);
    return true;
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
