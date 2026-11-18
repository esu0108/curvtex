#pragma once
#include <array>
#include <memory>
#include <vector>

#include "mesh/Mesh.h"

// メッシュの辺を UV 空間で表したもの。テクスチャタブの UV ワイヤーフレームの表示に使う。
// 同じ辺 (両端の UV が同じ) は 1 本にまとめてある。
struct UvEdges {
    std::vector<std::array<float, 4>> edges; // (u0, v0, u1, v1)
};

// mesh の三角形から辺を集める。両端が同じ点の辺や、UV が有限でない辺は含めない。
std::shared_ptr<const UvEdges> BuildUvEdges(const Mesh& mesh);
