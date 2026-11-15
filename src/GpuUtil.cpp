#include "GpuUtil.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gpu {

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
    const HRESULT    hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &err);
    if (FAILED(hr)) {
        throw std::runtime_error(std::string("RootSignature: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : ""));
    }
    ComPtr<ID3D12RootSignature> rs;
    Check(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs)), "CreateRootSignature");
    return rs;
}

ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* dev, const void* data, UINT64 size) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    const D3D12_RESOURCE_DESC rd = BufferDesc(size);
    ComPtr<ID3D12Resource>    res;
    Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)),
          "CreateCommittedResource(upload)");
    void*       p = nullptr;
    D3D12_RANGE r{0, 0};
    Check(res->Map(0, &r, &p), "Map");
    std::memcpy(p, data, static_cast<size_t>(size));
    res->Unmap(0, nullptr);
    return res;
}

ComPtr<ID3D12Resource> CreateRenderTargetTexture(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, const float clear[4], const char* what) {
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
    for (int i = 0; i < 4; ++i) cv.Color[i] = clear[i];
    ComPtr<ID3D12Resource> res;
    Check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&res)), what);
    return res;
}

} // namespace gpu
