#include <ufbx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mesh/Mesh.h"
#include "mesh/MeshBuild.h"
#include "i18n/Strings.h"
#include "mesh/MeshUtil.h"
#include "platform/PathUtil.h"

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

    if (!CheckInputFile(path, error)) return false;

    const std::string p = PathToUtf8(path);

    ufbx_load_opts opts{};
    ufbx_error     err{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene(ufbx_load_file(p.c_str(), &opts, &err));
    if (!scene) {
        error = Tr(Str::ErrFbxParse) + std::string(err.description.data, err.description.length);
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
                    const ufbx_vec3 pv      = mesh->vertex_position.values.data[k.p];
                    const double    pin[3]  = {pv.x, pv.y, pv.z};
                    double          nin[3]  = {0, 0, 0};
                    if (hasNormal) {
                        const ufbx_vec3 nv = mesh->vertex_normal.values.data[k.n];
                        nin[0] = nv.x;
                        nin[1] = nv.y;
                        nin[2] = nv.z;
                    }
                    const ufbx_vec2 uv = mesh->vertex_uv.values.data[k.t];
                    // FBX の v は上向き(本ツールと同じ)なので反転しない
                    const Vertex v = MakeWorldVertex(xf, pin, hasNormal ? nin : nullptr, static_cast<float>(uv.x), static_cast<float>(uv.y));
                    it = map.emplace(k, static_cast<uint32_t>(pm.vertices.size())).first;
                    pm.vertices.push_back(v);
                }
                pm.indices.push_back(it->second);
            }
        }
        if (pm.indices.empty()) continue;

        AppendPart(out, std::move(pm), xf.Mirrored(), hasNormal); // 鏡像なら巻き順反転 / 法線が無ければ生成 / out へ結合
    }

    return FinalizeMesh(out, error, missingUv, Tr(Str::ErrFbxNoUv));
}
