#include "mesh/Weld.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

struct PosKey {
    int64_t x, y, z;
    bool operator==(const PosKey&) const = default;
};
struct PosKeyHash {
    size_t operator()(const PosKey& k) const noexcept {
        size_t h = static_cast<size_t>(k.x) * 73856093u;
        h ^= static_cast<size_t>(k.y) * 19349663u;
        h ^= static_cast<size_t>(k.z) * 83492791u;
        return h;
    }
};

} // namespace

uint32_t WeldVertexIds(const Mesh& m, std::vector<uint32_t>& ids) {
    const double q = std::max(static_cast<double>(m.radius) * 1e-5, 1e-12);
    std::unordered_map<PosKey, uint32_t, PosKeyHash> weld;
    weld.reserve(m.vertices.size());
    ids.resize(m.vertices.size());
    uint32_t count = 0;
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const auto& v = m.vertices[i];
        const PosKey k{std::llround(v.px / q), std::llround(v.py / q), std::llround(v.pz / q)};
        auto         it = weld.find(k);
        if (it == weld.end()) it = weld.emplace(k, count++).first;
        ids[i] = it->second;
    }
    return count;
}
