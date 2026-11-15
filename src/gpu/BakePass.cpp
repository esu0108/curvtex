#include "gpu/BakePass.h"

#include <climits>
#include <cstring>
#include <iterator>
#include <vector>

// ビルド時に fxc でコンパイルされたシェーダ (shaders/*.hlsl → CMake が生成するヘッダ)
#include "jfa_vs.h"
#include "jfa_init_ps.h"
#include "jfa_step_ps.h"
#include "pass1_ps.h"
#include "pass1_vs.h"
#include "pass2_ps.h"
#include "pass2_vs.h"

using namespace gpu;

namespace {

// 法線バッファの形式。単位法線は [-1,1] に収まるので 16bit SNORM で足りる(精度は一様に約3e-5)。
// 16bit FLOAT は 1 付近で精度が粗く(約5e-4)、曲率の差分計算にノイズが乗るので使わない。
constexpr DXGI_FORMAT kNormalFormat   = DXGI_FORMAT_R16G16B16A16_SNORM;
constexpr DXGI_FORMAT kPositionFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT kResultFormat   = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kSeedFormat     = DXGI_FORMAT_R16G16_UINT; // 最寄りのテクセル座標 (最大 4096 なので 16bit で足りる)

// パス2のルート定数 (shaders/pass2.hlsl の cbuffer と同じ並び)
struct Pass2Constants {
    uint32_t w, h;
    float    invRange;
    uint32_t mode;
    uint32_t radius;
    uint32_t pad;
};
constexpr UINT kPass2ConstantCount = sizeof(Pass2Constants) / sizeof(uint32_t);

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
    normal_.Reset();
    position_.Reset();
    result_.Reset();
    seed_[0].Reset();
    seed_[1].Reset();
    w_ = w;
    h_ = h;

    auto* dev            = r_->Device();
    const float clear[4] = {0.f, 0.f, 0.f, 0.f}; // RecordBake の実際のクリア値と同じにしておく
    auto        make     = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res, UINT rtvSlot, UINT srvSlot) {
        res = CreateRenderTargetTexture(dev, w, h, fmt, clear, "CreateCommittedResource(bake target)");
        dev->CreateRenderTargetView(res.Get(), nullptr, r_->RtvCpu(rtvSlot));
        dev->CreateShaderResourceView(res.Get(), nullptr, r_->SrvCpu(srvSlot));
    };
    make(kNormalFormat, normal_, Slot::kRtvNormal, Slot::kSrvNormal);
    make(kPositionFormat, position_, Slot::kRtvPosition, Slot::kSrvPosition);
    make(kResultFormat, result_, Slot::kRtvResult, Slot::kSrvResult);
    make(kSeedFormat, seed_[0], Slot::kRtvSeedA, Slot::kSrvSeedA);
    make(kSeedFormat, seed_[1], Slot::kRtvSeedB, Slot::kSrvSeedB);
}

void BakePass::CreatePipelines() {
    auto* dev = r_->Device();

    // ---- パス1 ---------------------------------------------------------
    {
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rs1_      = MakeRootSignature(dev, rsd);

        const auto layout = GpuMesh::InputLayout();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rs1_.Get();
        pd.VS             = {g_Pass1VS, sizeof(g_Pass1VS)};
        pd.PS             = {g_Pass1PS, sizeof(g_Pass1PS)};
        for (int i = 0; i < 2; ++i) pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.InputLayout           = {layout.data(), static_cast<UINT>(layout.size())};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 2;
        pd.RTVFormats[0]         = kNormalFormat;
        pd.RTVFormats[1]         = kPositionFormat;
        pd.SampleDesc.Count      = 1;
        Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso1_)), "CreateGraphicsPipelineState(pass1)");
    }

    // ---- パス2 ---------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors                    = 2; // t0 = normal, t1 = position
        range.BaseShaderRegister                = 0;
        range.RegisterSpace                     = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        // t2 = ジャンプフラッディングの結果。ピンポンで A/B のどちらにもなるので、別のテーブルにする
        D3D12_DESCRIPTOR_RANGE seedRange = range;
        seedRange.NumDescriptors         = 1;
        seedRange.BaseShaderRegister     = 2;

        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.Num32BitValues = kPass2ConstantCount;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges   = &seedRange;
        params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 3;
        rsd.pParameters   = params;
        rs2_              = MakeRootSignature(dev, rsd);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rs2_.Get();
        pd.VS             = {g_Pass2VS, sizeof(g_Pass2VS)};
        pd.PS             = {g_Pass2PS, sizeof(g_Pass2PS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState       = DefaultRaster();
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kResultFormat;
        pd.SampleDesc.Count      = 1;
        Check(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso2_)), "CreateGraphicsPipelineState(pass2)");
    }

    // ---- ジャンプフラッディング (初期化 / ステップ) -----------------------
    {
        // 1つの SRV(単独のテーブル)を、指定のレジスタ(t0 or t1)で読むルートシグネチャを作る
        auto makeRoot = [&](UINT baseRegister) {
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors                    = 1;
            range.BaseShaderRegister                = baseRegister;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = kJfaConstantCount;
            params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges   = &range;
            params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsd{};
            rsd.NumParameters = 2;
            rsd.pParameters   = params;
            return MakeRootSignature(dev, rsd);
        };
        rsJfaInit_ = makeRoot(0); // PSInit: t0 = normal
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

UINT BakePass::RecordJumpFlood(uint32_t padding, const D3D12_VIEWPORT& vp, const D3D12_RECT& sc) {
    auto* cl = r_->CommandList();

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv[2] = {r_->RtvCpu(Slot::kRtvSeedA), r_->RtvCpu(Slot::kRtvSeedB)};
    const UINT                        srv[2] = {Slot::kSrvSeedA, Slot::kSrvSeedB};

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // seed_[i] へフルスクリーン三角形を1回描く (描画前後で RT <-> SRV を切り替える)
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
    draw(cur, rsJfaInit_.Get(), psoJfaInit_.Get(), 0, Slot::kSrvNormal);

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

void BakePass::Record(const GpuMesh& mesh, const Params& params) {
    if (!mesh.Valid()) return;
    auto* cl = r_->CommandList();

    const D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
    const D3D12_RECT     sc{0, 0, static_cast<LONG>(w_), static_cast<LONG>(h_)};
    const float          clear[4] = {0.f, 0.f, 0.f, 0.f};

    // ---- パス1: 法線 + 位置 を UV空間へ ----
    {
        D3D12_RESOURCE_BARRIER b[2] = {
            Transition(normal_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(position_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };
        cl->ResourceBarrier(2, b);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {r_->RtvCpu(Slot::kRtvNormal), r_->RtvCpu(Slot::kRtvPosition)};
        cl->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
        cl->ClearRenderTargetView(rtvs[0], clear, 0, nullptr);
        cl->ClearRenderTargetView(rtvs[1], clear, 0, nullptr);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);
        cl->SetGraphicsRootSignature(rs1_.Get());
        cl->SetPipelineState(pso1_.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mesh.Draw(cl);

        D3D12_RESOURCE_BARRIER back[2] = {
            Transition(normal_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(position_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        };
        cl->ResourceBarrier(2, back);
    }

    // ---- ジャンプフラッディング (パディング有効時のみ) ----
    // 無効なら seed は読まれないので、どちらのテクスチャを指していてもよい。
    UINT seedSlot = Slot::kSrvSeedA;
    if (params.padding > 0) seedSlot = RecordJumpFlood(params.padding, vp, sc);

    // ---- パス2: 法線から曲率 (+ パディング) ----
    {
        const Pass2Constants cb{w_, h_, params.invRange, params.mode, params.radius, params.padding};

        D3D12_RESOURCE_BARRIER toRt = Transition(result_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ResourceBarrier(1, &toRt);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = r_->RtvCpu(Slot::kRtvResult);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cl->ClearRenderTargetView(rtv, clear, 0, nullptr);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);

        ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetGraphicsRootSignature(rs2_.Get());
        cl->SetPipelineState(pso2_.Get());
        cl->SetGraphicsRoot32BitConstants(0, kPass2ConstantCount, &cb, 0);
        cl->SetGraphicsRootDescriptorTable(1, r_->SrvGpu(Slot::kSrvNormal)); // t0=normal, t1=position
        cl->SetGraphicsRootDescriptorTable(2, r_->SrvGpu(seedSlot));         // t2=最寄りのテクセル座標
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER b = Transition(result_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cl->ResourceBarrier(1, &b);
    }
}

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
