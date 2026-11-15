#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Mesh.h"
#include "MeshBuild.h"
#include "MeshUtil.h"

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

// 1つのプリミティブを三角形リストへ変換し out へ追加する。UV が無い/三角形でない場合は何もしない。
// 戻り値: 追加した場合 true。UV が無かった場合 missingUv を立てる。
bool AppendPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& prim, const Affine& xf, Mesh& out, bool& missingUv) {
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
    const fastgltf::Accessor* accUv  = find("TEXCOORD_0");
    if (!accPos || accPos->type != AccessorType::Vec3 || (!accPos->bufferViewIndex && !accPos->sparse)) return false;
    if (!accUv || accUv->type != AccessorType::Vec2 || (!accUv->bufferViewIndex && !accUv->sparse)) {
        missingUv = true;
        return false;
    }
    if (accUv->count != accPos->count) return false;
    const bool hasNormal = accNrm && accNrm->type == AccessorType::Vec3 && accNrm->count == accPos->count && (accNrm->bufferViewIndex || accNrm->sparse);

    const size_t n = accPos->count;
    if (n == 0) return false;

    std::vector<GltfV3> pos(n), nrm;
    std::vector<GltfV2> uv(n);
    fastgltf::iterateAccessorWithIndex<GltfV3>(asset, *accPos, [&](GltfV3 v, size_t i) { pos[i] = v; });
    fastgltf::iterateAccessorWithIndex<GltfV2>(asset, *accUv, [&](GltfV2 v, size_t i) { uv[i] = v; });
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

    // ワールド(ノード変換後)の頂点へ。glTF の v は下向き(左上原点)なので、本ツールの v 上向きに合わせて反転する。
    Mesh part;
    part.vertices.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double p[3]  = {pos[i].x, pos[i].y, pos[i].z};
        const double nn[3] = {hasNormal ? nrm[i].x : 0.0, hasNormal ? nrm[i].y : 0.0, hasNormal ? nrm[i].z : 0.0};
        part.vertices[i]   = MakeWorldVertex(xf, p, hasNormal ? nn : nullptr, uv[i].x, 1.0f - uv[i].y);
    }
    part.indices = std::move(tris);
    AppendPart(out, std::move(part), xf.Mirrored(), hasNormal); // 鏡像なら巻き順反転 / 法線が無ければ生成 / out へ結合
    return true;
}

} // namespace

bool LoadGltf(const std::filesystem::path& path, Mesh& out, std::string& error) {
    out = Mesh{};

    if (!CheckInputFile(path, error)) return false;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        error = "glTF読み込みエラー: " + ErrorText(data.error());
        return false;
    }

    // .gltf / .glb は loadGltf が自動判別する。外部 .bin は LoadExternalBuffers で読み込む。
    fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization);
    auto             asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (asset.error() != fastgltf::Error::None) {
        error = "glTF解析エラー: " + ErrorText(asset.error());
        return false;
    }
    fastgltf::Asset& a = asset.get();

    bool missingUv = false;
    auto addMesh   = [&](size_t meshIndex, const Affine& xf) {
        if (meshIndex >= a.meshes.size()) return;
        for (const auto& prim : a.meshes[meshIndex].primitives) AppendPrimitive(a, prim, xf, out, missingUv);
    };

    if (a.scenes.empty()) {
        // シーン定義が無い場合は全メッシュを恒等変換で読む
        for (size_t i = 0; i < a.meshes.size(); ++i) addMesh(i, Affine{});
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
                                        addMesh(*node.meshIndex, xf);
                                    });
    }

    return FinalizeMesh(out, error, missingUv, "このglTFにはUV座標(TEXCOORD_0)がありません。UV展開してから書き出してください");
}
