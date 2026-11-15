#pragma once
#include <array>

#include "gpu/GpuUtil.h"
#include "mesh/Mesh.h"
#include "gpu/Renderer.h"

// GPU 上の頂点/インデックスバッファ。ベイク(パス1)と3Dビューで共有する。
class GpuMesh {
public:
    // CPU のメッシュを GPU へ転送する。GPU を待ってから古いバッファを差し替える。
    void Upload(Renderer& renderer, const Mesh& mesh);

    bool Valid() const { return indexCount_ != 0; }

    // 境界球 (ベイクで位置を正規化するのに使う)
    const float* Center() const { return center_; }
    float        Radius() const { return radius_; }

    // 頂点/インデックスバッファをバインドして描画する(プリミティブトポロジは呼び出し側が設定済みであること)
    void Draw(ID3D12GraphicsCommandList* cl) const;

    // Vertex 構造体に対応する入力レイアウト(POSITION / NORMAL / TEXCOORD0)
    static std::array<D3D12_INPUT_ELEMENT_DESC, 3> InputLayout();

private:
    gpu::ComPtr<ID3D12Resource> vb_, ib_;
    D3D12_VERTEX_BUFFER_VIEW    vbv_{};
    D3D12_INDEX_BUFFER_VIEW     ibv_{};
    UINT                        indexCount_ = 0;
    float                       center_[3]  = {0, 0, 0};
    float                       radius_     = 1.0f;
};
