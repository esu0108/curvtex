#include "mesh/UvEdges.h"

#include <algorithm>
#include <cmath>

std::shared_ptr<const UvEdges> BuildUvEdges(const Mesh& mesh) {
    auto out = std::make_shared<UvEdges>();
    auto& e  = out->edges;
    e.reserve(mesh.indices.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            const Vertex& a = mesh.vertices[mesh.indices[i + k]];
            const Vertex& b = mesh.vertices[mesh.indices[i + (k + 1) % 3]];
            if (!std::isfinite(a.u) || !std::isfinite(a.v) || !std::isfinite(b.u) || !std::isfinite(b.v)) continue;
            // 向きをそろえて (小さい方を先頭に) 同じ辺を同じ並びにする。+0.0f で -0.0 を +0.0 にそろえる。
            std::array<float, 4> d = {a.u + 0.0f, a.v + 0.0f, b.u + 0.0f, b.v + 0.0f};
            if (d[0] > d[2] || (d[0] == d[2] && d[1] > d[3])) {
                std::swap(d[0], d[2]);
                std::swap(d[1], d[3]);
            }
            if (d[0] == d[2] && d[1] == d[3]) continue; // 長さ 0
            e.push_back(d);
        }
    }
    std::sort(e.begin(), e.end());
    e.erase(std::unique(e.begin(), e.end()), e.end());
    e.shrink_to_fit();
    return out;
}
