#include "ViewPass.h"

#include <algorithm>
#include <climits>
#include <cstring>

#include "view_ps.h"
#include "view_vs.h"

using namespace gpu;

namespace {

// 背景色(クリア値と最適化クリア値を揃える)
constexpr float kClear[4] = {0.16f, 0.16f, 0.19f, 1.0f};

// ルート定数 (shaders/view.hlsl の cbuffer と同じ並び): mvp(16) + view(16) + lit(1) + パディング(3)
constexpr UINT kViewConstants = 36;

} // namespace

void ViewPass::Init(Renderer& renderer) {
    r_ = &renderer;
    CreatePipeline();
    CreateTargets(w_, h_);
}

void ViewPass::Resize(UINT w, UINT h) {
    w = std::clamp<UINT>(w, 16, 8192);
    h = std::clamp<UINT>(h, 16, 8192);
    if (w == w_ && h == h_) return;
    CreateTargets(w, h);
}

void ViewPass::CreateTargets(UINT w, UINT h) {
    r_->WaitIdle();
    color_.Reset();
    depth_.Reset();
    w_ = w;
    h_ = h;

    auto* dev = r_->Device();

    // カラー (ImGui::Image で表示するので SRV も作る)
    color_ = CreateRenderTargetTexture(dev, w, h, Renderer::kBackFormat, kClear, "CreateCommittedResource(view color)");
    dev->CreateRenderTargetView(color_.Get(), nullptr, r_->RtvCpu(Slot::kRtvView));
    dev->CreateShaderResourceView(color_.Get(), nullptr, r_->SrvCpu(Slot::kSrvView));

    // 深度
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format             = DXGI_FORMAT_D32_FLOAT;
    dcv.DepthStencil.Depth = 1.0f;
    Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&depth_)),
          "CreateCommittedResource(view depth)");
    dev->CreateDepthStencilView(depth_.Get(), nullptr, r_->DsvCpu());
}

void ViewPass::CreatePipeline() {
    auto* dev = r_->Device();

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
    rs_                   = MakeRootSignature(dev, rsd);

    const auto layout = GpuMesh::InputLayout();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rs_.Get();
    pd.VS             = {g_ViewVS, sizeof(g_ViewVS)};
    pd.PS             = {g_ViewPS, sizeof(g_ViewPS)};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask                            = UINT_MAX;
    pd.RasterizerState                       = DefaultRaster();
    pd.RasterizerState.FrontCounterClockwise = TRUE; // 右手系・反時計回りが表(SV_IsFrontFace 用)。両面描画なのでカリングはしない
    pd.DepthStencilState.DepthEnable         = TRUE;
    pd.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc           = D3D12_COMPARISON_FUNC_LESS;
    pd.DSVFormat                             = DXGI_FORMAT_D32_FLOAT;
    pd.InputLayout                           = {layout.data(), static_cast<UINT>(layout.size())};
    pd.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets                      = 1;
    pd.RTVFormats[0]                         = Renderer::kBackFormat;
    pd.SampleDesc.Count                      = 1;
    Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_)), "CreateGraphicsPipelineState(view)");
}

void ViewPass::Record(const GpuMesh& mesh, const BakePass& bake, const float* mvp16, const float* view16, bool lit) {
    if (!mesh.Valid() || !color_) return;
    auto* cl = r_->CommandList();

    D3D12_RESOURCE_BARRIER toRt = Transition(color_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &toRt);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(Slot::kRtvView);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = r_->DsvCpu();
    cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cl->ClearRenderTargetView(rtv, kClear, 0, nullptr);
    cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    float cb[kViewConstants] = {};
    std::memcpy(cb, mvp16, 16 * sizeof(float));
    std::memcpy(cb + 16, view16, 16 * sizeof(float));
    cb[32] = lit ? 1.0f : 0.0f;

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rs_.Get());
    cl->SetPipelineState(pso_.Get());
    cl->SetGraphicsRoot32BitConstants(0, kViewConstants, cb, 0);
    cl->SetGraphicsRootDescriptorTable(1, bake.ResultSrv()); // t0 = 曲率テクスチャ(結果)
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mesh.Draw(cl);

    D3D12_RESOURCE_BARRIER toSrv = Transition(color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &toSrv);
}
