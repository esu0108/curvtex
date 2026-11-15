#include "gpu/BakePass.h"

#include <climits>
#include <cstring>
#include <iterator>
#include <vector>

// ビルド時に fxc でコンパイルされたシェーダ (shaders/*.hlsl → CMake が生成するヘッダ)
#include "jfa_init_ps.h"
#include "jfa_step_ps.h"
#include "jfa_vs.h"
#include "raster_ps.h"
#include "raster_vs.h"
#include "shade_ps.h"
#include "shade_vs.h"

using namespace gpu;

namespace {

// 曲率テクスチャ: x = 曲率(1/単位長, 32bit float), y = カバレッジ。
constexpr DXGI_FORMAT kCurvFormat   = DXGI_FORMAT_R32G32_FLOAT;
constexpr DXGI_FORMAT kResultFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kSeedFormat   = DXGI_FORMAT_R16G16_UINT; // 最寄りのテクセル座標 (最大 4096 なので 16bit で足りる)

// シェーディングのルート定数 (shaders/shade.hlsl の cbuffer と同じ並び)
struct ShadeConstants {
    uint32_t w, h;
    float    invRange;
    uint32_t mode;
    uint32_t pad;
};
constexpr UINT kShadeConstantCount = sizeof(ShadeConstants) / sizeof(uint32_t);

// ジャンプフラッディングのルート定数 (shaders/jfa.hlsl の cbuffer と同じ並び)
struct JfaConstants {
    uint32_t w, h;
    int32_t  step;
    uint32_t pad;
};
constexpr UINT kJfaConstantCount = sizeof(JfaConstants) / sizeof(uint32_t);

// 幅 padding テクセル以内の最近傍を求めるのに必要な、最初のステップ幅。
// ステップ s0, s0/2, ..., 1 の合計は 2*s0-1 なので、これが padding 以上になる最小の 2 の冪を選ぶ。
int InitialJumpStep(uint32_t padding) {
    int s0 = 1;
    while (2 * s0 - 1 < static_cast<int>(padding)) s0 *= 2;
    return s0;
}

D3D12_DESCRIPTOR_RANGE SrvRange(UINT baseRegister) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = baseRegister;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

D3D12_ROOT_PARAMETER SrvTable(const D3D12_DESCRIPTOR_RANGE* range, D3D12_SHADER_VISIBILITY vis) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = 1;
    p.DescriptorTable.pDescriptorRanges   = range;
    p.ShaderVisibility                    = vis;
    return p;
}

D3D12_ROOT_PARAMETER PixelConstants(UINT count) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p.Constants.ShaderRegister = 0;
    p.Constants.Num32BitValues = count;
    p.ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    return p;
}

} // namespace

void BakePass::Init(Renderer& renderer) {
    r_ = &renderer;
    CreatePipelines();
    CreateTargets(w_, h_);
}

void BakePass::Resize(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    CreateTargets(width, height);
}

void BakePass::CreateTargets(UINT w, UINT h) {
    r_->WaitIdle();
    curvTex_.Reset();
    result_.Reset();
    seed_[0].Reset();
    seed_[1].Reset();
    rasterReady_ = false;
    w_           = w;
    h_           = h;

    auto* dev            = r_->Device();
    const float clear[4] = {0.f, 0.f, 0.f, 0.f}; // 実際のクリア値と同じにしておく
    auto        make     = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res, UINT rtvSlot, UINT srvSlot) {
        res = CreateRenderTargetTexture(dev, w, h, fmt, clear, "CreateCommittedResource(bake target)");
        dev->CreateRenderTargetView(res.Get(), nullptr, r_->RtvCpu(rtvSlot));
        dev->CreateShaderResourceView(res.Get(), nullptr, r_->SrvCpu(srvSlot));
    };
    make(kCurvFormat, curvTex_, Slot::kRtvCurv, Slot::kSrvCurvTex);
    make(kResultFormat, result_, Slot::kRtvResult, Slot::kSrvResult);
    make(kSeedFormat, seed_[0], Slot::kRtvSeedA, Slot::kSrvSeedA);
    make(kSeedFormat, seed_[1], Slot::kRtvSeedB, Slot::kSrvSeedB);
}

void BakePass::CreatePipelines() {
    auto* dev = r_->Device();

    // ---- ラスタライズ: 頂点シェーダーが StructuredBuffer を2つ読む ---------
    {
        const D3D12_DESCRIPTOR_RANGE nodeMap = SrvRange(0); // t0 = 頂点 → ノード
        const D3D12_DESCRIPTOR_RANGE curv    = SrvRange(1); // t1 = ノードごとの曲率 (スムージング結果。A/B で入れ替わるので別テーブル)
        D3D12_ROOT_PARAMETER         params[2] = {SrvTable(&nodeMap, D3D12_SHADER_VISIBILITY_VERTEX), SrvTable(&curv, D3D12_SHADER_VISIBILITY_VERTEX)};

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters   = params;
        rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rsRaster_         = MakeRootSignature(dev, rsd);

        const auto layout = GpuMesh::InputLayout();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rsRaster_.Get();
        pd.VS             = {g_RasterVS, sizeof(g_RasterVS)};
        pd.PS             = {g_RasterPS, sizeof(g_RasterPS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.InputLayout           = {layout.data(), static_cast<UINT>(layout.size())};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kCurvFormat;
        pd.SampleDesc.Count      = 1;
        Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoRaster_)), "CreateGraphicsPipelineState(raster)");
    }

    // ---- シェーディング ---------------------------------------------------
    {
        const D3D12_DESCRIPTOR_RANGE curvTex = SrvRange(0); // t0 = 曲率テクスチャ
        const D3D12_DESCRIPTOR_RANGE seed    = SrvRange(1); // t1 = 最寄りのテクセル座標 (ピンポンで A/B のどちらにもなるので別テーブル)
        D3D12_ROOT_PARAMETER         params[3] = {PixelConstants(kShadeConstantCount), SrvTable(&curvTex, D3D12_SHADER_VISIBILITY_PIXEL),
                                                  SrvTable(&seed, D3D12_SHADER_VISIBILITY_PIXEL)};

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 3;
        rsd.pParameters   = params;
        rsShade_          = MakeRootSignature(dev, rsd);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rsShade_.Get();
        pd.VS             = {g_ShadeVS, sizeof(g_ShadeVS)};
        pd.PS             = {g_ShadePS, sizeof(g_ShadePS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kResultFormat;
        pd.SampleDesc.Count      = 1;
        Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoShade_)), "CreateGraphicsPipelineState(shade)");
    }

    // ---- ジャンプフラッディング (初期化 / ステップ) -----------------------
    {
        // 1つの SRV(単独のテーブル)を、指定のレジスタ(t0 or t1)で読むルートシグネチャを作る
        auto makeRoot = [&](UINT baseRegister) {
            const D3D12_DESCRIPTOR_RANGE range = SrvRange(baseRegister);
            D3D12_ROOT_PARAMETER         params[2] = {PixelConstants(kJfaConstantCount), SrvTable(&range, D3D12_SHADER_VISIBILITY_PIXEL)};
            D3D12_ROOT_SIGNATURE_DESC    rsd{};
            rsd.NumParameters = 2;
            rsd.pParameters   = params;
            return MakeRootSignature(dev, rsd);
        };
        rsJfaInit_ = makeRoot(0); // PSInit: t0 = 曲率テクスチャ(カバレッジ)
        rsJfaStep_ = makeRoot(1); // PSStep: t1 = 前のパスのシード

        auto makePso = [&](ID3D12RootSignature* rs, const void* ps, size_t psSize, const char* what) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = rs;
            pd.VS             = {g_JfaVS, sizeof(g_JfaVS)};
            pd.PS             = {ps, psSize};
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pd.SampleMask            = UINT_MAX;
            pd.RasterizerState       = DefaultRaster();
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets      = 1;
            pd.RTVFormats[0]         = kSeedFormat;
            pd.SampleDesc.Count      = 1;
            ComPtr<ID3D12PipelineState> pso;
            Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), what);
            return pso;
        };
        psoJfaInit_ = makePso(rsJfaInit_.Get(), g_JfaInitPS, sizeof(g_JfaInitPS), "CreateGraphicsPipelineState(jfa init)");
        psoJfaStep_ = makePso(rsJfaStep_.Get(), g_JfaStepPS, sizeof(g_JfaStepPS), "CreateGraphicsPipelineState(jfa step)");
    }
}

// ---------------------------------------------------------------------------
// ラスタライズ
// ---------------------------------------------------------------------------
void BakePass::RecordRaster(const GpuMesh& mesh, const CurvatureCompute& curvature) {
    if (!mesh.Valid() || !curvature.Ready()) return;
    auto* cl = r_->CommandList();

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)};
    const float          clear[4] = {0.f, 0.f, 0.f, 0.f};

    D3D12_RESOURCE_BARRIER toRt = Transition(curvTex_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &toRt);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(Slot::kRtvCurv);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rsRaster_.Get());
    cl->SetPipelineState(psoRaster_.Get());
    cl->SetGraphicsRootDescriptorTable(0, curvature.NodeMapSrv()); // t0
    cl->SetGraphicsRootDescriptorTable(1, curvature.FinalSrv());   // t1
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mesh.Draw(cl);

    D3D12_RESOURCE_BARRIER toSrv = Transition(curvTex_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &toSrv);
    rasterReady_ = true;
}

// ---------------------------------------------------------------------------
// パディング (ジャンプフラッディング)
// ---------------------------------------------------------------------------
UINT BakePass::RecordJumpFlood(uint32_t padding) {
    auto* cl = r_->CommandList();

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)};

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv[2] = {r_->RtvCpu(Slot::kRtvSeedA), r_->RtvCpu(Slot::kRtvSeedB)};
    const UINT                        srv[2] = {Slot::kSrvSeedA, Slot::kSrvSeedB};

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // seed_[dst] へフルスクリーン三角形を1回描く (描画前後で RT <-> SRV を切り替える)
    auto draw = [&](int dst, ID3D12RootSignature* rs, ID3D12PipelineState* pso, int32_t step, UINT readSlot) {
        D3D12_RESOURCE_BARRIER toRt = Transition(seed_[dst].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ResourceBarrier(1, &toRt);

        D3D12_CPU_DESCRIPTOR_HANDLE target = rtv[dst];
        cl->OMSetRenderTargets(1, &target, FALSE, nullptr);
        const JfaConstants cb{w_, h_, step, padding};
        cl->SetGraphicsRootSignature(rs);
        cl->SetPipelineState(pso);
        cl->SetGraphicsRoot32BitConstants(0, kJfaConstantCount, &cb, 0);
        cl->SetGraphicsRootDescriptorTable(1, r_->SrvGpu(readSlot));
        cl->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER toSrv = Transition(seed_[dst].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &toSrv);
    };

    // 初期化: モデルのあるテクセルは自分自身がシード
    int cur = 0;
    draw(cur, rsJfaInit_.Get(), psoJfaInit_.Get(), 0, Slot::kSrvCurvTex);

    // ステップ幅 s0, s0/2, ..., 1 を順に適用し、最後にもう一度 1 を適用する(JFA+1: 取りこぼしを減らす)
    std::vector<int32_t> steps;
    for (int s = InitialJumpStep(padding); s >= 1; s /= 2) steps.push_back(s);
    steps.push_back(1);
    for (int32_t step : steps) {
        const int dst = 1 - cur;
        draw(dst, rsJfaStep_.Get(), psoJfaStep_.Get(), step, srv[cur]);
        cur = dst;
    }
    return srv[cur];
}

void BakePass::RecordPadding(uint32_t padding) {
    if (!rasterReady_) return;
    // パディング無効なら seed は読まれないので、どちらのテクスチャを指していてもよい。
    seedSlot_ = (padding > 0) ? RecordJumpFlood(padding) : Slot::kSrvSeedA;
}

// ---------------------------------------------------------------------------
// シェーディング
// ---------------------------------------------------------------------------
void BakePass::RecordShading(const Params& params) {
    if (!rasterReady_) return;
    auto* cl = r_->CommandList();

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)};
    const float          clear[4] = {0.f, 0.f, 0.f, 0.f};
    const ShadeConstants cb{w_, h_, params.invRange, params.mode, params.padding};

    D3D12_RESOURCE_BARRIER toRt = Transition(result_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &toRt);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(Slot::kRtvResult);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rsShade_.Get());
    cl->SetPipelineState(psoShade_.Get());
    cl->SetGraphicsRoot32BitConstants(0, kShadeConstantCount, &cb, 0);
    cl->SetGraphicsRootDescriptorTable(1, r_->SrvGpu(Slot::kSrvCurvTex)); // t0 = 曲率テクスチャ
    cl->SetGraphicsRootDescriptorTable(2, r_->SrvGpu(seedSlot_));         // t1 = 最寄りのテクセル座標
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toSrv = Transition(result_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &toSrv);
}

// ---------------------------------------------------------------------------
// 読み戻し
// ---------------------------------------------------------------------------
bool BakePass::Readback(std::vector<uint8_t>& rgba) {
    if (!result_) return false;
    auto* dev = r_->Device();
    r_->WaitIdle();

    const D3D12_RESOURCE_DESC          rd = result_->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT                               rows    = 0;
    UINT64                             rowSize = 0, total = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSize, &total);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    const D3D12_RESOURCE_DESC bd = BufferDesc(total);
    ComPtr<ID3D12Resource>    rb;
    Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)),
          "CreateCommittedResource(readback)");

    r_->ExecuteImmediate([&](ID3D12GraphicsCommandList* cl) {
        D3D12_RESOURCE_BARRIER b1 = Transition(result_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cl->ResourceBarrier(1, &b1);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource       = rb.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource        = result_.Get();
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER b2 = Transition(result_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &b2);
    });

    uint8_t*    mapped = nullptr;
    D3D12_RANGE rr{0, static_cast<SIZE_T>(total)};
    Check(rb->Map(0, &rr, reinterpret_cast<void**>(&mapped)), "Map(readback)");
    const UINT w = static_cast<UINT>(rd.Width), h = rd.Height;
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (UINT y = 0; y < h; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch, static_cast<size_t>(w) * 4);
    }
    D3D12_RANGE wr{0, 0};
    rb->Unmap(0, &wr);
    return true;
}
