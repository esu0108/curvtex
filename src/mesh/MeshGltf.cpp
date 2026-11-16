#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "mesh/Mesh.h"
#include "mesh/MeshBuild.h"
#include "i18n/Strings.h"
#include "mesh/MeshUtil.h"

// accessor ツールで直接読み込むための単純な型(fastgltf::math のベクトル型に依存しない)
struct GltfV2 {
    float x = 0, y = 0;
};
struct GltfV3 {
    float x = 0, y = 0, z = 0;
};
template <>
struct fastgltf::ElementTraits<GltfV2> : fastgltf::ElementTraitsBase<GltfV2, fastgltf::AccessorType::Vec2, float> {};
template <>
struct fastgltf::ElementTraits<GltfV3> : fastgltf::ElementTraitsBase<GltfV3, fastgltf::AccessorType::Vec3, float> {};

namespace {

std::string ErrorText(fastgltf::Error e) {
    return std::string(fastgltf::getErrorName(e)) + ": " + std::string(fastgltf::getErrorMessage(e));
}

template <class S>
std::string ToStd(const S& s) {
    return std::string(s.begin(), s.end());
}

// 1つのプリミティブを三角形リストへ変換し scene へパーツとして追加する。UV が無い/三角形でない場合は何もしない。
// meshId はこのメッシュ(ノード)の番号で、最初にパーツを追加できたときに meshName で登録する (負なら未登録)。
// 戻り値: 追加した場合 true。UV が無かった場合 missingUv を立てる。
bool AppendPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& prim, const Affine& xf, ModelScene& scene, int& meshId,
                     const std::string& meshName, bool& missingUv) {
    using fastgltf::AccessorType;
    using fastgltf::PrimitiveType;
    if (prim.type != PrimitiveType::Triangles && prim.type != PrimitiveType::TriangleStrip && prim.type != PrimitiveType::TriangleFan) return false;

    auto find = [&](const char* name) -> const fastgltf::Accessor* {
        for (const auto& at : prim.attributes) {
            if (at.name == name) return &asset.accessors[at.accessorIndex];
        }
        return nullptr;
    };
    const fastgltf::Accessor* accPos = find("POSITION");
    const fastgltf::Accessor* accNrm = find("NORMAL");
    if (!accPos || accPos->type != AccessorType::Vec3 || (!accPos->bufferViewIndex && !accPos->sparse)) return false;

    // UV: TEXCOORD_0, 1, ... を順に集める。TEXCOORD_0 は必須。2番目以降で読めないものは空のチャンネルにする。
    std::vector<const fastgltf::Accessor*> accUvs;
    for (int c = 0; c < kMaxUvChannels; ++c) {
        const std::string          name = "TEXCOORD_" + std::to_string(c);
        const fastgltf::Accessor* a    = find(name.c_str());
        if (!a) break;
        const bool ok = a->type == AccessorType::Vec2 && (a->bufferViewIndex || a->sparse);
        accUvs.push_back(ok ? a : nullptr);
    }
    if (accUvs.empty() || !accUvs[0]) {
        missingUv = true;
        return false;
    }
    if (accUvs[0]->count != accPos->count) return false;
    for (auto& a : accUvs) {
        if (a && a->count != accPos->count) a = nullptr;
    }
    const bool hasNormal = accNrm && accNrm->type == AccessorType::Vec3 && accNrm->count == accPos->count && (accNrm->bufferViewIndex || accNrm->sparse);

    const size_t n = accPos->count;
    if (n == 0) return false;

    std::vector<GltfV3> pos(n), nrm;
    fastgltf::iterateAccessorWithIndex<GltfV3>(asset, *accPos, [&](GltfV3 v, size_t i) { pos[i] = v; });
    if (hasNormal) {
        nrm.resize(n);
        fastgltf::iterateAccessorWithIndex<GltfV3>(asset, *accNrm, [&](GltfV3 v, size_t i) { nrm[i] = v; });
    }

    // インデックス(無ければ連番)
    std::vector<uint32_t> idx;
    if (prim.indicesAccessor) {
        const auto& ai = asset.accessors[*prim.indicesAccessor];
        if (ai.type != AccessorType::Scalar) return false;
        idx.reserve(ai.count);
        fastgltf::iterateAccessor<std::uint32_t>(asset, ai, [&](std::uint32_t v) { idx.push_back(v); });
    } else {
        idx.resize(n);
        for (size_t i = 0; i < n; ++i) idx[i] = static_cast<uint32_t>(i);
    }

    // 三角形リストへ展開(範囲外インデックスを含む三角形は捨てる)
    std::vector<uint32_t> tris;
    auto                  push = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a >= n || b >= n || c >= n) return;
        tris.push_back(a);
        tris.push_back(b);
        tris.push_back(c);
    };
    switch (prim.type) {
    case PrimitiveType::Triangles:
        for (size_t i = 0; i + 2 < idx.size(); i += 3) push(idx[i], idx[i + 1], idx[i + 2]);
        break;
    case PrimitiveType::TriangleStrip:
        for (size_t i = 0; i + 2 < idx.size(); ++i) {
            if (i % 2 == 0) push(idx[i], idx[i + 1], idx[i + 2]);
            else push(idx[i + 1], idx[i], idx[i + 2]);
        }
        break;
    case PrimitiveType::TriangleFan:
        for (size_t i = 1; i + 1 < idx.size(); ++i) push(idx[0], idx[i], idx[i + 1]);
        break;
    default: break;
    }
    if (tris.empty()) return false;

    // ワールド(ノード変換後)の頂点へ。UV はチャンネルごとに別の配列へ入れる。
    // glTF の v は下向き(左上原点)なので、本ツールの v 上向きに合わせて反転する。
    MeshPart part;
    part.materialIndex = prim.materialIndex ? static_cast<int>(*prim.materialIndex) : -1;
    part.geo.vertices.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double p[3]  = {pos[i].x, pos[i].y, pos[i].z};
        const double nn[3] = {hasNormal ? nrm[i].x : 0.0, hasNormal ? nrm[i].y : 0.0, hasNormal ? nrm[i].z : 0.0};
        part.geo.vertices[i] = MakeWorldVertex(xf, p, hasNormal ? nn : nullptr, 0.0f, 0.0f);
    }
    part.uvs.resize(accUvs.size());
    for (size_t c = 0; c < accUvs.size(); ++c) {
        if (!accUvs[c]) continue;
        part.uvs[c].resize(n);
        fastgltf::iterateAccessorWithIndex<GltfV2>(asset, *accUvs[c], [&](GltfV2 v, size_t i) { part.uvs[c][i] = {v.x, 1.0f - v.y}; });
    }
    part.geo.indices = std::move(tris);

    if (meshId < 0) {
        meshId = static_cast<int>(scene.meshNames.size());
        scene.meshNames.push_back(meshName);
    }
    part.meshIndex = meshId;
    AddPart(scene, std::move(part), xf.Mirrored(), hasNormal); // 鏡像なら巻き順反転 / 法線が無ければ生成 / scene へ追加
    return true;
}

} // namespace

bool LoadGltf(const std::filesystem::path& path, ModelScene& out, std::string& error) {
    out = ModelScene{};

    if (!CheckInputFile(path, error)) return false;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        error = Tr(Str::ErrGltfLoad) + ErrorText(data.error());
        return false;
    }

    // .gltf / .glb は loadGltf が自動判別する。外部 .bin は LoadExternalBuffers で読み込む。
    fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization);
    auto             asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (asset.error() != fastgltf::Error::None) {
        error = Tr(Str::ErrGltfParse) + ErrorText(asset.error());
        return false;
    }
    fastgltf::Asset& a = asset.get();

    out.materialNames.reserve(a.materials.size());
    for (const auto& m : a.materials) out.materialNames.push_back(ToStd(m.name));

    // メッシュ = シーンの中でメッシュを持つノード1つ (同じメッシュを複数のノードが使えば、別のメッシュとして並ぶ)
    bool missingUv = false;
    auto addMesh   = [&](size_t meshIndex, const Affine& xf, const std::string& nodeName) {
        if (meshIndex >= a.meshes.size()) return;
        const std::string name   = nodeName.empty() ? ToStd(a.meshes[meshIndex].name) : nodeName;
        int               meshId = -1;
        for (const auto& prim : a.meshes[meshIndex].primitives) AppendPrimitive(a, prim, xf, out, meshId, name, missingUv);
    };

    if (a.scenes.empty()) {
        // シーン定義が無い場合は全メッシュを恒等変換で読む
        for (size_t i = 0; i < a.meshes.size(); ++i) addMesh(i, Affine{}, std::string());
    } else {
        const size_t sceneIndex = a.defaultScene.value_or(0);
        fastgltf::iterateSceneNodes(a, sceneIndex < a.scenes.size() ? sceneIndex : 0, fastgltf::math::nmat4x4(),
                                    [&](fastgltf::Node& node, fastgltf::math::nmat4x4 m) {
                                        if (!node.meshIndex) return;
                                        // 列優先の 4x4 を素の float 配列へ(glTF 仕様と同じ列優先)
                                        static_assert(sizeof(fastgltf::math::nmat4x4) == 16 * sizeof(float), "FASTGLTF_USE_64BIT_FLOAT は未対応");
                                        float f[16];
                                        std::memcpy(f, &m, sizeof(f));
                                        Affine xf;
                                        for (int col = 0; col < 4; ++col)
                                            for (int row = 0; row < 3; ++row) xf.c[col][row] = f[col * 4 + row];
                                        addMesh(*node.meshIndex, xf, ToStd(node.name));
                                    });
    }

    return FinalizeScene(out, error, missingUv, Tr(Str::ErrGltfNoUv));
}
