#include <ufbx.h>

#include <algorithm>
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

// 頂点の同一判定: 位置・法線と、全 UV チャンネルのインデックスが同じなら同じ頂点
struct Key {
    uint32_t                                p, n;
    std::array<uint32_t, kMaxUvChannels>    t{};
    bool operator==(const Key&) const = default;
};
struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = k.p;
        h        = h * 1000003u ^ k.n;
        for (uint32_t v : k.t) h = h * 1000003u ^ v;
        return h;
    }
};

// 1メッシュ内のマテリアルごとの作業用データ
struct Group {
    MeshPart                                   part;
    std::unordered_map<Key, uint32_t, KeyHash> map;
};

struct SceneDeleter {
    void operator()(ufbx_scene* s) const { ufbx_free_scene(s); }
};

} // namespace

bool LoadFbx(const std::filesystem::path& path, ModelScene& out, std::string& error) {
    out = ModelScene{};

    if (!CheckInputFile(path, error)) return false;

    const std::string p = PathToUtf8(path);

    ufbx_load_opts opts{};
    ufbx_error     err{};
    std::unique_ptr<ufbx_scene, SceneDeleter> scene(ufbx_load_file(p.c_str(), &opts, &err));
    if (!scene) {
        error = Tr(Str::ErrFbxParse) + std::string(err.description.data, err.description.length);
        return false;
    }

    out.materialNames.reserve(scene->materials.count);
    for (size_t mi = 0; mi < scene->materials.count; ++mi) {
        const ufbx_string& nm = scene->materials.data[mi]->name;
        out.materialNames.emplace_back(nm.data, nm.length);
    }

    // メッシュ = メッシュを持つノード1つ。ノードの中は、マテリアルごとに別のパーツにする。
    bool missingUv = false;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->faces.count == 0) continue;
        const size_t nUv = std::min<size_t>(mesh->uv_sets.count, kMaxUvChannels);
        if (nUv == 0) {
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

        const size_t       matCount = mesh->materials.count;
        std::vector<Group> groups(matCount + 1); // 最後の1つは「マテリアルなし」
        for (Group& g : groups) g.part.uvs.resize(nUv);
        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);

        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            if (face.num_indices < 3) continue;

            size_t gi = matCount;
            if (fi < mesh->face_material.count) {
                const uint32_t mi = mesh->face_material.data[fi];
                if (mi < matCount) gi = mi;
            }
            Group& g = groups[gi];

            const uint32_t numTris = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t i = 0; i < numTris * 3; ++i) {
                const uint32_t ix = tri[i];
                Key            k{};
                k.p = mesh->vertex_position.indices.data[ix];
                k.n = hasNormal ? mesh->vertex_normal.indices.data[ix] : 0u;
                for (size_t c = 0; c < nUv; ++c) k.t[c] = mesh->uv_sets.data[c].vertex_uv.indices.data[ix];

                auto it = g.map.find(k);
                if (it == g.map.end()) {
                    const ufbx_vec3 pv     = mesh->vertex_position.values.data[k.p];
                    const double    pin[3] = {pv.x, pv.y, pv.z};
                    double          nin[3] = {0, 0, 0};
                    if (hasNormal) {
                        const ufbx_vec3 nv = mesh->vertex_normal.values.data[k.n];
                        nin[0] = nv.x;
                        nin[1] = nv.y;
                        nin[2] = nv.z;
                    }
                    const Vertex v = MakeWorldVertex(xf, pin, hasNormal ? nin : nullptr, 0.0f, 0.0f);
                    it = g.map.emplace(k, static_cast<uint32_t>(g.part.geo.vertices.size())).first;
                    g.part.geo.vertices.push_back(v);
                    // FBX の v は上向き(本ツールと同じ)なので反転しない
                    for (size_t c = 0; c < nUv; ++c) {
                        const ufbx_vec2 uv = mesh->uv_sets.data[c].vertex_uv.values.data[k.t[c]];
                        g.part.uvs[c].push_back({static_cast<float>(uv.x), static_cast<float>(uv.y)});
                    }
                }
                g.part.geo.indices.push_back(it->second);
            }
        }

        int meshId = -1;
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            MeshPart& part = groups[gi].part;
            if (part.geo.indices.empty()) continue;
            if (meshId < 0) {
                meshId = static_cast<int>(out.meshNames.size());
                out.meshNames.emplace_back(node->name.data, node->name.length);
            }
            part.meshIndex     = meshId;
            part.materialIndex = gi < matCount ? static_cast<int>(mesh->materials.data[gi]->typed_id) : -1;
            AddPart(out, std::move(part), xf.Mirrored(), hasNormal); // 鏡像なら巻き順反転 / 法線が無ければ生成 / out へ追加
        }
    }

    return FinalizeScene(out, error, missingUv, Tr(Str::ErrFbxNoUv));
}
