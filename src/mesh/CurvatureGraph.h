#pragma once
#include <cstdint>
#include <vector>

#include "mesh/Mesh.h"

// 頂点ごとの曲率計算用の隣接グラフ。GPU のコンピュートシェーダーが読む形式(CSR)で持つ。
//   位置が同じ頂点(UV継ぎ目・ハードエッジで分割された頂点)は 1 つのノードに溶接する。
//   曲率はノードごとに求め、元の頂点はノードの値を共有する。これで継ぎ目をまたいでも値が連続する。
struct CurvatureGraph {
    std::vector<float>    positions;    // ノードの位置 (x,y,z) * N
    std::vector<float>    normals;      // ノードの法線 (x,y,z) * N。同じ位置の頂点の法線を平均して正規化したもの
    std::vector<uint32_t> offsets;      // N+1 個。ノード i の隣接は neighbors[offsets[i] .. offsets[i+1])
    std::vector<uint32_t> neighbors;    // 隣接ノード番号 (重複なし・自分自身を含まない)
    std::vector<uint32_t> vertexToNode; // 元の頂点 → ノード番号

    uint32_t NodeCount() const { return static_cast<uint32_t>(positions.size() / 3); }
};

// mesh.radius が計算済みで、法線が設定済み(必要ならスムース法線を再計算済み)であること。
CurvatureGraph BuildCurvatureGraph(const Mesh& mesh);
