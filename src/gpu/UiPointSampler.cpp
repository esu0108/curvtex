#include "gpu/UiPointSampler.h"

#include <climits>
#include <cstddef>

#include "imgui.h"

using namespace gpu;

UiPointSampler& UiPointSampler::Instance() {
    static UiPointSampler inst;
    return inst;
}

void UiPointSampler::Init(ID3D12Device* dev, DXGI_FORMAT rtvFormat) {
    // ルートシグネチャ: ImGui のバックエンドと同じ並び (0: 投影行列の 16 個の定数, 1: テクスチャの SRV テーブル) + 点サンプラー。
    // 並びを揃えているので、この後にバックエンドが SetGraphicsRootDescriptorTable(1, ...) で貼るテクスチャがそのまま使える。
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 16;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &range;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxAnisotropy    = 1;
    samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
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

    // 頂点レイアウトは ImDrawVert (pos: float2, uv: float2, col: RGBA8)
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, pos)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(ImDrawVert, col)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    const ShaderBlob vsBlob = LoadShader("ui_vs"), psBlob = LoadShader("ui_ps");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rs_.Get();
    pd.VS             = vsBlob.Bytecode();
    pd.PS             = psBlob.Bytecode();
    pd.InputLayout    = {layout, 3};
    // ブレンドはバックエンド (imgui_impl_dx12.cpp) と同じ: 通常のアルファブレンド
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    rt.BlendEnable           = TRUE;
    rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp               = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask            = UINT_MAX;
    pd.RasterizerState       = DefaultRaster();
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = rtvFormat;
    pd.SampleDesc.Count      = 1;
    Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_)), "CreateGraphicsPipelineState(ui point)");
}

void UiPointSampler::Shutdown() {
    cl_ = nullptr;
    pso_.Reset();
    rs_.Reset();
}

void UiPointSampler::BeginFrame(ID3D12GraphicsCommandList* cl, const ImDrawData* dd) {
    cl_ = cl;
    if (!dd) return;
    // imgui_impl_dx12.cpp の SetupRenderState と同じ投影行列
    const float L = dd->DisplayPos.x;
    const float R = dd->DisplayPos.x + dd->DisplaySize.x;
    const float T = dd->DisplayPos.y;
    const float B = dd->DisplayPos.y + dd->DisplaySize.y;
    const float m[4][4] = {
        {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
        {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
        {0.0f, 0.0f, 0.5f, 0.0f},
        {(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f},
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) mvp_[i][j] = m[i][j];
}

void UiPointSampler::Begin(const ImDrawList*, const ImDrawCmd*) {
    UiPointSampler& s = Instance();
    if (!s.cl_ || !s.pso_) return;
    s.cl_->SetGraphicsRootSignature(s.rs_.Get()); // ルートシグネチャを替えると、設定済みのルート引数は無効になる
    s.cl_->SetPipelineState(s.pso_.Get());
    s.cl_->SetGraphicsRoot32BitConstants(0, 16, s.mvp_, 0);
}
