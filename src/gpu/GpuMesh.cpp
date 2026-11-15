#include "gpu/GpuMesh.h"

#include <cstddef>

using namespace gpu;

void GpuMesh::Upload(Renderer& renderer, const Mesh& mesh) {
    renderer.WaitIdle();
    vb_.Reset();
    ib_.Reset();
    indexCount_ = 0;

    const UINT64 vbSize = mesh.vertices.size() * sizeof(Vertex);
    const UINT64 ibSize = mesh.indices.size() * sizeof(uint32_t);
    vb_                 = CreateUploadBuffer(renderer.Device(), mesh.vertices.data(), vbSize);
    ib_                 = CreateUploadBuffer(renderer.Device(), mesh.indices.data(), ibSize);

    vbv_.BufferLocation = vb_->GetGPUVirtualAddress();
    vbv_.SizeInBytes    = static_cast<UINT>(vbSize);
    vbv_.StrideInBytes  = sizeof(Vertex);
    ibv_.BufferLocation = ib_->GetGPUVirtualAddress();
    ibv_.SizeInBytes    = static_cast<UINT>(ibSize);
    ibv_.Format         = DXGI_FORMAT_R32_UINT;
    indexCount_         = static_cast<UINT>(mesh.indices.size());
}

void GpuMesh::Draw(ID3D12GraphicsCommandList* cl) const {
    cl->IASetVertexBuffers(0, 1, &vbv_);
    cl->IASetIndexBuffer(&ibv_);
    cl->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

std::array<D3D12_INPUT_ELEMENT_DESC, 3> GpuMesh::InputLayout() {
    return {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, px), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, nx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
}
