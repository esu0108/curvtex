#include "gpu/ViewPass.h"

#include <algorithm>
#include <climits>
#include <cstring>


using namespace gpu;

namespace {

// 背景色(クリア値と最適化クリア値を揃える)
constexpr float kClear[4] = {0.16f, 0.16f, 0.19f, 1.0f};

// ルート定数 (shaders/view.hlsl の cbuffer と同じ並び): mvp(16) + view(16) + lit(1) + パディング(3)
constexpr UINT kViewConstants = 36;

// ミップ付きテクスチャの形式。BakePass の結果テクスチャ(R8G8B8A8_UNORM)と同じにすること。
constexpr DXGI_FORMAT kMipFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// 1 つのサブリソース(ミップレベル)だけのバリア
D3D12_RESOURCE_BARRIER SubresourceTransition(ID3D12Resource* r, UINT subresource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = Transition(r, from, to);
    b.Transition.Subresource = subresource;
    return b;
}

} // namespace

void ViewPass::Init(Renderer& renderer) {
    r_ = &renderer;

    // 対応しているサンプル数を調べる (カラーと深度の両方が対応している必要がある)
    msaaMask_ = 0;
    for (UINT n : {2u, 4u, 8u}) {
        bool ok = true;
        for (DXGI_FORMAT fmt : {Renderer::kBackFormat, DXGI_FORMAT_D32_FLOAT}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
            q.Format      = fmt;
            q.SampleCount = n;
            ok = ok && SUCCEEDED(r_->Device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) && q.NumQualityLevels > 0;
        }
        if (ok) msaaMask_ |= 1u << n;
    }

    CreatePipeline();
    CreateMipPipeline();
    CreateTargets(w_, h_);
}

void ViewPass::Resize(UINT w, UINT h) {
    w = std::clamp<UINT>(w, 16, 8192);
    h = std::clamp<UINT>(h, 16, 8192);
    if (w == w_ && h == h_) return;
    CreateTargets(w, h);
}

void ViewPass::CreateMipPipeline() {
    auto* dev = r_->Device();

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 2; // 出力サイズの逆数 (x, y)
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &range;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxAnisotropy    = 1;
    samp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 2;
    rsd.pParameters       = params;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers   = &samp;
    rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rsMip_                = MakeRootSignature(dev, rsd);

    const ShaderBlob vsBlob = LoadShader("mip_vs"), psBlob = LoadShader("mip_ps");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rsMip_.Get();
    pd.VS             = vsBlob.Bytecode();
    pd.PS             = psBlob.Bytecode();
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask            = UINT_MAX;
    pd.RasterizerState       = DefaultRaster();
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = kMipFormat;
    pd.SampleDesc.Count      = 1;
    Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoMip_)), "CreateGraphicsPipelineState(mip)");
}

void ViewPass::PrepareTexture(UINT w, UINT h) {
    if (w == 0 || h == 0) return;
    if (mipTex_ && w == mipW_ && h == mipH_) return;
    CreateMipTexture(w, h);
}

void ViewPass::CreateMipTexture(UINT w, UINT h) {
    r_->WaitIdle();
    mipTex_.Reset();
    mipW_       = w;
    mipH_       = h;
    mipVersion_ = 0; // 作り直したので、次の Record で必ず中身を作る

    // レベル数: 長い辺が 1 になるまで (4096 なら 13)
    UINT levels = 1;
    for (UINT s = std::max(w, h); s > 1; s >>= 1) ++levels;
    mipLevels_ = std::min(levels, Slot::kMaxMipLevels);

    auto* dev = r_->Device();

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = static_cast<UINT16>(mipLevels_);
    rd.Format           = kMipFormat;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&mipTex_)),
          "CreateCommittedResource(view mip texture)");

    for (UINT lvl = 0; lvl < mipLevels_; ++lvl) {
        // 描画先: レベルごとの RTV
        D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
        rtvd.Format               = kMipFormat;
        rtvd.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvd.Texture2D.MipSlice   = lvl;
        dev->CreateRenderTargetView(mipTex_.Get(), &rtvd, r_->RtvCpu(Slot::kRtvMip + lvl));

        // 縮小の入力: 次のレベルを作るときに、このレベルだけを読む SRV (最後のレベルは読まれない)
        if (lvl + 1 < mipLevels_) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
            srvd.Format                  = kMipFormat;
            srvd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvd.Texture2D.MostDetailedMip = lvl;
            srvd.Texture2D.MipLevels       = 1;
            dev->CreateShaderResourceView(mipTex_.Get(), &srvd, r_->SrvCpu(Slot::kSrvMipSrc + lvl));
        }
    }
    // 3Dビューが貼る SRV: 全レベル
    dev->CreateShaderResourceView(mipTex_.Get(), nullptr, r_->SrvCpu(Slot::kSrvViewTex));
}

void ViewPass::RecordMips(const BakePass& bake) {
    if (!mipTex_ || mipVersion_ == bake.Version()) return;
    mipVersion_ = bake.Version();
    auto* cl    = r_->CommandList();

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rsMip_.Get());
    cl->SetPipelineState(psoMip_.Get());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // レベル 0 は曲率テクスチャ本体から (同じサイズなのでそのまま写る)、以降は 1 つ上のレベルから縮小する。
    // レベルごとに RENDER_TARGET へ移して描き、描き終えたら次のレベルの入力にするため PIXEL_SHADER_RESOURCE へ戻す。
    UINT lw = mipW_, lh = mipH_;
    for (UINT lvl = 0; lvl < mipLevels_; ++lvl) {
        D3D12_RESOURCE_BARRIER toRt = SubresourceTransition(mipTex_.Get(), lvl, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ResourceBarrier(1, &toRt);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(Slot::kRtvMip + lvl);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(lw), static_cast<float>(lh), 0.f, 1.f};
        const D3D12_RECT     sc{0, 0, static_cast<LONG>(lw), static_cast<LONG>(lh)};
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);

        const float invSize[2] = {1.0f / static_cast<float>(lw), 1.0f / static_cast<float>(lh)};
        cl->SetGraphicsRoot32BitConstants(0, 2, invSize, 0);
        cl->SetGraphicsRootDescriptorTable(1, lvl == 0 ? bake.ResultSrv() : r_->SrvGpu(Slot::kSrvMipSrc + lvl - 1));
        cl->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER toSrv = SubresourceTransition(mipTex_.Get(), lvl, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &toSrv);

        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
}

void ViewPass::SetQuality(UINT msaaSamples, UINT anisotropy) {
    // 対応していないサンプル数は、対応する最大の値(無ければ 1)に丸める
    UINT samples = 1;
    for (UINT n : {2u, 4u, 8u}) {
        if (n <= msaaSamples && SupportsMsaa(n)) samples = n;
    }
    const UINT aniso = std::clamp<UINT>(anisotropy, 1, 16);
    if (samples == samples_ && aniso == aniso_) return;

    r_->WaitIdle(); // 使用中のルートシグネチャ / PSO / ターゲットを作り直すので、GPU の完了を待つ
    const bool samplesChanged = samples != samples_;
    samples_                  = samples;
    aniso_                    = aniso;
    CreatePipeline();
    if (samplesChanged) CreateTargets(w_, h_);
}

void ViewPass::CreateTargets(UINT w, UINT h) {
    r_->WaitIdle();
    color_.Reset();
    msaaColor_.Reset();
    depth_.Reset();
    w_ = w;
    h_ = h;

    auto* dev = r_->Device();

    // カラー (ImGui::Image で表示するので SRV も作る)
    color_ = CreateRenderTargetTexture(dev, w, h, Renderer::kBackFormat, kClear, "CreateCommittedResource(view color)");
    dev->CreateRenderTargetView(color_.Get(), nullptr, r_->RtvCpu(Slot::kRtvView));
    dev->CreateShaderResourceView(color_.Get(), nullptr, r_->SrvCpu(Slot::kSrvView));

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    // MSAA: マルチサンプルの描画先 (描画後に color_ へ解決する)
    if (samples_ > 1) {
        D3D12_RESOURCE_DESC md{};
        md.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        md.Width            = w;
        md.Height           = h;
        md.DepthOrArraySize = 1;
        md.MipLevels        = 1;
        md.Format           = Renderer::kBackFormat;
        md.SampleDesc.Count = samples_;
        md.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        md.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE mcv{};
        mcv.Format = Renderer::kBackFormat;
        std::memcpy(mcv.Color, kClear, sizeof(kClear));
        Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &md, D3D12_RESOURCE_STATE_RENDER_TARGET, &mcv, IID_PPV_ARGS(&msaaColor_)),
              "CreateCommittedResource(view msaa color)");
        dev->CreateRenderTargetView(msaaColor_.Get(), nullptr, r_->RtvCpu(Slot::kRtvViewMsaa));
    }

    // 深度 (サンプル数は描画先と同じにする)
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc.Count = samples_;
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
    samp.Filter           = aniso_ > 1 ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.MaxAnisotropy    = aniso_;
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

    const ShaderBlob vsBlob = LoadShader("view_vs"), psBlob = LoadShader("view_ps");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rs_.Get();
    pd.VS             = vsBlob.Bytecode();
    pd.PS             = psBlob.Bytecode();
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
    pd.SampleDesc.Count                      = samples_;
    Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_)), "CreateGraphicsPipelineState(view)");
}

void ViewPass::Record(const GpuMesh& mesh, const BakePass& bake, const float* mvp16, const float* view16, bool lit) {
    if (!mesh.Valid() || !color_ || !mipTex_) return;
    auto* cl = r_->CommandList();

    RecordMips(bake); // 曲率テクスチャが更新されていたら、ミップを作り直す (3Dビューの描画より前)

    // MSAA 時は msaaColor_ (常に RENDER_TARGET 状態)へ描いて color_ へ解決する。そうでなければ color_ へ直接描く。
    const bool msaa = samples_ > 1 && msaaColor_;
    if (!msaa) {
        D3D12_RESOURCE_BARRIER toRt = Transition(color_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ResourceBarrier(1, &toRt);
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(msaa ? Slot::kRtvViewMsaa : Slot::kRtvView);
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
    cl->SetGraphicsRootDescriptorTable(1, r_->SrvGpu(Slot::kSrvViewTex)); // t0 = 曲率テクスチャ (ミップ付き)
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mesh.Draw(cl);

    if (msaa) {
        D3D12_RESOURCE_BARRIER toResolve[2] = {
            Transition(msaaColor_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
            Transition(color_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST),
        };
        cl->ResourceBarrier(2, toResolve);
        cl->ResolveSubresource(color_.Get(), 0, msaaColor_.Get(), 0, Renderer::kBackFormat);
        D3D12_RESOURCE_BARRIER back[2] = {
            Transition(msaaColor_.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(color_.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        };
        cl->ResourceBarrier(2, back);
    } else {
        D3D12_RESOURCE_BARRIER toSrv = Transition(color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &toSrv);
    }
}
