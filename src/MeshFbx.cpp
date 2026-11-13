#include <ufbx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mesh.h"
#include "MeshUtil.h"

namespace {

struct Key {
    uint32_t p, t, n;
    bool operator==(const Key&) const = default;
};
struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = k.p;
        h        = h * 1000003u ^ k.t;
        h        = h * 1000003u ^ k.n;
        return h;
    }
};

struct SceneDeleter {
    void operator()(ufbx_scene* s) const { ufbx_free_scene(s); }
};

} // namespace

bool LoadFbx(const std::filesystem::path& path, Mesh& out, std::string& error) {
    out = Mesh{};

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = "ファイルが見つかりません（パスを確認してください）";
        return false;
    }

    const std::u8string u8 = path.u8string();
    const std::string   p(u8.begin(), u8.end());

    ufbx_load_opts opts{};
    ufbx_error     err{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene(ufbx_load_file(p.c_str(), &opts, &err));
    if (!scene) {
        error = "FBX解析エラー: " + std::string(err.description.data, err.description.length);
        return false;
    }

    bool missingUv = false;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->faces.count == 0) continue;
        if (!mesh->vertex_uv.exists) {
            missingUv = true;
            continue;
        }
        const bool hasNormal = mesh->vertex_normal.exists;

        // ノードのジオメトリ→ワールド変換(ufbx_matrix は列優先 4x3)
        const ufbx_matrix& m = node->geometry_to_world;
        Affine             xf;
        xf.c[0][0] = m.m00; xf.c[0][1] = m.m10; xf.c[0][2] = m.m20;
        xf.c[1][0] = m.m01; xf.c[1][1] = m.m11; xf.c[1][2] = m.m21;
        xf.c[2][0] = m.m02; xf.c[2][1] = m.m12; xf.c[2][2] = m.m22;
        xf.c[3][0] = m.m03; xf.c[3][1] = m.m13; xf.c[3][2] = m.m23;

        Mesh                                       pm;
        std::unordered_map<Key, uint32_t, KeyHash> map;
        std::vector<uint32_t>                      tri(mesh->max_face_triangles * 3);

        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            if (face.num_indices < 3) continue;
            const uint32_t numTris = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t i = 0; i < numTris * 3; ++i) {
                const uint32_t ix = tri[i];
                const Key      k{mesh->vertex_position.indices.data[ix], mesh->vertex_uv.indices.data[ix],
                                 hasNormal ? mesh->vertex_normal.indices.data[ix] : 0u};
                auto           it = map.find(k);
                if (it == map.end()) {
                    Vertex       v{};
                    const ufbx_vec3 pv = mesh->vertex_position.values.data[k.p];
                    const double pin[3] = {pv.x, pv.y, pv.z};
                    float        pw[3];
                    xf.Point(pin, pw);
                    v.px = pw[0];
                    v.py = pw[1];
                    v.pz = pw[2];
                    if (hasNormal) {
                        const ufbx_vec3 nv = mesh->vertex_normal.values.data[k.n];
                        const double    nin[3] = {nv.x, nv.y, nv.z};
                        float           nw[3];
                        xf.Normal(nin, nw);
                        v.nx = nw[0];
                        v.ny = nw[1];
                        v.nz = nw[2];
                    }
                    const ufbx_vec2 uv = mesh->vertex_uv.values.data[k.t];
                    v.u                = static_cast<float>(uv.x); // FBX の v は上向き(本ツールと同じ)なので反転しない
                    v.v                = static_cast<float>(uv.y);
                    it                 = map.emplace(k, static_cast<uint32_t>(pm.vertices.size())).first;
                    pm.vertices.push_back(v);
                }
                pm.indices.push_back(it->second);
            }
        }
        if (pm.indices.empty()) continue;

        if (xf.Mirrored()) { // 鏡像変換では巻き順が反転する
            for (size_t i = 0; i + 2 < pm.indices.size(); i += 3) std::swap(pm.indices[i + 1], pm.indices[i + 2]);
        }
        if (!hasNormal) { // 法線が無いメッシュはスムース法線を生成
            ComputeBounds(pm);
            RecomputeSmoothNormals(pm);
        }

        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        out.vertices.insert(out.vertices.end(), pm.vertices.begin(), pm.vertices.end());
        for (uint32_t i : pm.indices) out.indices.push_back(base + i);
    }

    if (out.indices.empty()) {
        error = missingUv ? "このFBXにはUV座標がありません。UV展開してから書き出してください" : "三角形がありません";
        return false;
    }
    ComputeBounds(out);
    return true;
}
