#include "gpu/CurvatureCompute.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "curvature_cs.h"
#include "smooth_cs.h"

using namespace gpu;

namespace {

constexpr UINT kGroupSize = 256; // shaders/curvature.hlsl の numthreads と同じ

// StructuredBuffer の SRV を作る
void CreateBufferSrv(ID3D12Device* dev, ID3D12Resource* res, UINT count, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format                     = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Buffer.NumElements         = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateShaderResourceView(res, &d, h);
}

void CreateBufferUav(ID3D12Device* dev, ID3D12Resource* res, UINT count, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format                     = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements         = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateUnorderedAccessView(res, nullptr, &d, h);
}

D3D12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT count, UINT baseRegister) {
    D3D12_DESCRIPTOR_RANGE r{};
    r.RangeType                         = type;
    r.NumDescriptors                    = count;
    r.BaseShaderRegister                = baseRegister;
    r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return r;
}

D3D12_ROOT_PARAMETER Table(const D3D12_DESCRIPTOR_RANGE* range) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = 1;
    p.DescriptorTable.pDescriptorRanges   = range;
    p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    return p;
}

D3D12_ROOT_PARAMETER Constants(UINT count) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p.Constants.ShaderRegister = 0;
    p.Constants.Num32BitValues = count;
    p.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    return p;
}

} // namespace

void CurvatureCompute::Init(Renderer& renderer) {
    r_        = &renderer;
    auto* dev = r_->Device();

    const D3D12_DESCRIPTOR_RANGE graph = Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0); // t0..t3 = 位置, 法線, offsets, neighbors
    const D3D12_DESCRIPTOR_RANGE in    = Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // t4 = 入力の曲率
    const D3D12_DESCRIPTOR_RANGE out   = Range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0 = 出力の曲率

    {
        D3D12_ROOT_PARAMETER params[3] = {Constants(1), Table(&graph), Table(&out)};
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 3;
        rsd.pParameters   = params;
        rsCurv_           = MakeRootSignature(dev, rsd);
    }
    {
        D3D12_ROOT_PARAMETER params[4] = {Constants(1), Table(&graph), Table(&in), Table(&out)};
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 4;
        rsd.pParameters   = params;
        rsSmooth_         = MakeRootSignature(dev, rsd);
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rsCurv_.Get();
    pd.CS             = {g_CurvatureCS, sizeof(g_CurvatureCS)};
    Check(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&psoCurv_)), "CreateComputePipelineState(curvature)");
    pd.pRootSignature = rsSmooth_.Get();
    pd.CS             = {g_SmoothCS, sizeof(g_SmoothCS)};
    Check(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&psoSmooth_)), "CreateComputePipelineState(smooth)");
}

void CurvatureCompute::SetGraph(const CurvatureGraph& g) {
    r_->WaitIdle();
    positions_.Reset();
    normals_.Reset();
    offsets_.Reset();
    neighbors_.Reset();
    nodeMap_.Reset();
    for (auto& c : curv_) c.Reset();
    nodeCount_ = 0;
    rawReady_  = false;

    auto*          dev = r_->Device();
    const uint32_t n   = g.NodeCount();
    if (n == 0) return;

    // ---- 読み取り専用のグラフ: UPLOAD ヒープ経由で DEFAULT ヒープへコピー ----
    struct Item {
        ComPtr<ID3D12Resource>* dst;
        const void*             data;
        UINT64                  bytes;
        UINT                    stride;
        UINT                    count;
        UINT                    slot;
    };
    const Item items[] = {
        {&positions_, g.positions.data(), g.positions.size() * sizeof(float), 12, n, Slot::kSrvGraph + 0},
        {&normals_, g.normals.data(), g.normals.size() * sizeof(float), 12, n, Slot::kSrvGraph + 1},
        {&offsets_, g.offsets.data(), g.offsets.size() * sizeof(uint32_t), 4, static_cast<UINT>(g.offsets.size()), Slot::kSrvGraph + 2},
        {&neighbors_, g.neighbors.data(), g.neighbors.size() * sizeof(uint32_t), 4, static_cast<UINT>(std::max<size_t>(g.neighbors.size(), 1)), Slot::kSrvGraph + 3},
        {&nodeMap_, g.vertexToNode.data(), g.vertexToNode.size() * sizeof(uint32_t), 4, static_cast<UINT>(g.vertexToNode.size()), Slot::kSrvNodeMap},
    };

    std::vector<ComPtr<ID3D12Resource>> staging;
    for (const Item& it : items) {
        const UINT64 size = std::max<UINT64>(it.bytes, 16); // 空のバッファ(隣接なし)でも作れるように
        // バッファは必ず COMMON で作られる (COPY_DEST などを指定しても無視されて警告が出る)。最初のコピーで暗黙に COPY_DEST へ昇格する。
        *it.dst = CreateDefaultBuffer(dev, size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource(graph)");
        if (it.bytes > 0) {
            staging.push_back(CreateUploadBuffer(dev, it.data, it.bytes));
        } else {
            const uint32_t zero[4] = {};
            staging.push_back(CreateUploadBuffer(dev, zero, sizeof(zero)));
        }
        CreateBufferSrv(dev, it.dst->Get(), it.count, it.stride, r_->SrvCpu(it.slot));
    }
    r_->ExecuteImmediate([&](ID3D12GraphicsCommandList* cl) {
        for (size_t i = 0; i < std::size(items); ++i) {
            cl->CopyBufferRegion(items[i].dst->Get(), 0, staging[i].Get(), 0, items[i].bytes > 0 ? items[i].bytes : 16);
        }
        std::vector<D3D12_RESOURCE_BARRIER> b;
        for (const Item& it : items) b.push_back(gpu::Transition(it.dst->Get(), D3D12_RESOURCE_STATE_COPY_DEST, kRead));
        cl->ResourceBarrier(static_cast<UINT>(b.size()), b.data());
    });

    // ---- 頂点ごとの曲率 (UAV で書き、SRV で読む) ----
    const UINT srvSlots[3] = {Slot::kSrvCurvRaw, Slot::kSrvCurvA, Slot::kSrvCurvB};
    const UINT uavSlots[3] = {Slot::kUavCurvRaw, Slot::kUavCurvA, Slot::kUavCurvB};
    for (int i = 0; i < 3; ++i) {
        curv_[i] = CreateDefaultBuffer(dev, static_cast<UINT64>(n) * sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource(curvature)");
        CreateBufferSrv(dev, curv_[i].Get(), n, sizeof(float), r_->SrvCpu(srvSlots[i]));
        CreateBufferUav(dev, curv_[i].Get(), n, sizeof(float), r_->SrvCpu(uavSlots[i]));
    }
    // COMMON で作られているので、Dispatch が前提にしている「読み取り状態(kRead)」へ明示的に移しておく
    r_->ExecuteImmediate([&](ID3D12GraphicsCommandList* cl) {
        D3D12_RESOURCE_BARRIER b[3];
        for (int i = 0; i < 3; ++i) b[i] = gpu::Transition(curv_[i].Get(), D3D12_RESOURCE_STATE_COMMON, kRead);
        cl->ResourceBarrier(3, b);
    });
    nodeCount_ = n;
    finalSlot_ = Slot::kSrvCurvRaw;
}

void CurvatureCompute::Transition(int buf, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    const D3D12_RESOURCE_BARRIER b = gpu::Transition(curv_[buf].Get(), from, to);
    r_->CommandList()->ResourceBarrier(1, &b);
}

// dst へ書く。src < 0 なら CSCurvature、そうでなければ curv_[src] を入力にした CSSmooth。
void CurvatureCompute::Dispatch(int dst, int src) {
    auto* cl = r_->CommandList();

    Transition(dst, kRead, kUav);

    ID3D12DescriptorHeap* heaps[] = {r_->SrvHeap()};
    cl->SetDescriptorHeaps(1, heaps);

    const UINT uavSlots[3] = {Slot::kUavCurvRaw, Slot::kUavCurvA, Slot::kUavCurvB};
    const UINT srvSlots[3] = {Slot::kSrvCurvRaw, Slot::kSrvCurvA, Slot::kSrvCurvB};
    if (src < 0) {
        cl->SetComputeRootSignature(rsCurv_.Get());
        cl->SetPipelineState(psoCurv_.Get());
        cl->SetComputeRoot32BitConstants(0, 1, &nodeCount_, 0);
        cl->SetComputeRootDescriptorTable(1, r_->SrvGpu(Slot::kSrvGraph));
        cl->SetComputeRootDescriptorTable(2, r_->SrvGpu(uavSlots[dst]));
    } else {
        cl->SetComputeRootSignature(rsSmooth_.Get());
        cl->SetPipelineState(psoSmooth_.Get());
        cl->SetComputeRoot32BitConstants(0, 1, &nodeCount_, 0);
        cl->SetComputeRootDescriptorTable(1, r_->SrvGpu(Slot::kSrvGraph));
        cl->SetComputeRootDescriptorTable(2, r_->SrvGpu(srvSlots[src]));
        cl->SetComputeRootDescriptorTable(3, r_->SrvGpu(uavSlots[dst]));
    }
    cl->Dispatch((nodeCount_ + kGroupSize - 1) / kGroupSize, 1, 1);

    Transition(dst, kUav, kRead); // 次のディスパッチ / 頂点シェーダーから読めるようにする (UAV の書き込み完了も保証される)
}

void CurvatureCompute::RecordRaw() {
    if (!Ready()) return;
    Dispatch(0, -1);
    rawReady_  = true;
    finalSlot_ = Slot::kSrvCurvRaw;
}

void CurvatureCompute::RecordSmoothing(uint32_t iterations) {
    if (!Ready() || !rawReady_) return;
    // 生 → A → B → A ... とピンポン。生の値は残るので、回数を変えてもやり直せる。
    int src = 0;
    for (uint32_t i = 0; i < iterations; ++i) {
        const int dst = (i % 2 == 0) ? 1 : 2;
        Dispatch(dst, src);
        src = dst;
    }
    const UINT srvSlots[3] = {Slot::kSrvCurvRaw, Slot::kSrvCurvA, Slot::kSrvCurvB};
    finalSlot_             = srvSlots[src];
}
