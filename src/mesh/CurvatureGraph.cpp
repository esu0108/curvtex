#include "mesh/CurvatureGraph.h"

#include <algorithm>
#include <cmath>

#include "mesh/Weld.h"

CurvatureGraph BuildCurvatureGraph(const Mesh& m) {
    CurvatureGraph g;
    const uint32_t n = WeldVertexIds(m, g.vertexToNode);

    // ---- ノードの位置と法線 ----
    g.positions.assign(static_cast<size_t>(n) * 3, 0.0f);
    std::vector<double>  nacc(static_cast<size_t>(n) * 3, 0.0);
    std::vector<uint8_t> seen(n, 0);
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const Vertex&  v = m.vertices[i];
        const uint32_t w = g.vertexToNode[i];
        if (!seen[w]) { // 位置は最初に現れた頂点のものを使う (溶接許容幅の範囲でしか違わない)
            seen[w]                     = 1;
            g.positions[w * 3 + 0]      = v.px;
            g.positions[w * 3 + 1]      = v.py;
            g.positions[w * 3 + 2]      = v.pz;
        }
        nacc[w * 3 + 0] += v.nx;
        nacc[w * 3 + 1] += v.ny;
        nacc[w * 3 + 2] += v.nz;
    }
    g.normals.resize(static_cast<size_t>(n) * 3);
    for (uint32_t w = 0; w < n; ++w) {
        const double x = nacc[w * 3 + 0], y = nacc[w * 3 + 1], z = nacc[w * 3 + 2];
        const double l = std::sqrt(x * x + y * y + z * z);
        if (l > 1e-20) {
            g.normals[w * 3 + 0] = static_cast<float>(x / l);
            g.normals[w * 3 + 1] = static_cast<float>(y / l);
            g.normals[w * 3 + 2] = static_cast<float>(z / l);
        } else { // 法線が打ち消し合った(退化)場合の保険
            g.normals[w * 3 + 0] = 0.0f;
            g.normals[w * 3 + 1] = 0.0f;
            g.normals[w * 3 + 2] = 1.0f;
        }
    }

    // ---- 隣接 (CSR) : 三角形の辺から。重複あり → 行ごとに整列して重複を除く ----
    std::vector<uint32_t> deg(n + 1, 0);
    auto                  forEachEdge = [&](auto&& fn) {
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const uint32_t a = g.vertexToNode[m.indices[t]];
            const uint32_t b = g.vertexToNode[m.indices[t + 1]];
            const uint32_t c = g.vertexToNode[m.indices[t + 2]];
            if (a != b) fn(a, b);
            if (b != c) fn(b, c);
            if (c != a) fn(c, a);
        }
    };
    forEachEdge([&](uint32_t a, uint32_t b) {
        ++deg[a];
        ++deg[b];
    });

    std::vector<uint32_t> start(n + 1, 0); // 重複を含む行の開始位置
    for (uint32_t i = 0; i < n; ++i) start[i + 1] = start[i] + deg[i];
    std::vector<uint32_t> raw(start[n]);
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    forEachEdge([&](uint32_t a, uint32_t b) {
        raw[fill[a]++] = b;
        raw[fill[b]++] = a;
    });

    g.offsets.assign(n + 1, 0);
    g.neighbors.reserve(raw.size());
    for (uint32_t i = 0; i < n; ++i) {
        auto first = raw.begin() + start[i];
        auto last  = raw.begin() + start[i + 1];
        std::sort(first, last);
        last = std::unique(first, last);
        g.neighbors.insert(g.neighbors.end(), first, last);
        g.offsets[i + 1] = static_cast<uint32_t>(g.neighbors.size());
    }
    return g;
}
