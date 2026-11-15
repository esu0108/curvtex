#include "mesh/Mesh.h"
#include "mesh/MeshBuild.h"
#include "mesh/Weld.h"

#include <rapidobj/rapidobj.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cwctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace {

struct Key {
    int p, t, n;
    bool operator==(const Key&) const = default;
};

struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = static_cast<uint32_t>(k.p);
        h = h * 1000003u ^ static_cast<uint32_t>(k.t);
        h = h * 1000003u ^ static_cast<uint32_t>(k.n);
        return h;
    }
};

void Normalize3(float* v) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-20f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    } else {
        v[0] = 0;
        v[1] = 0;
        v[2] = 1;
    }
}

} // namespace

bool LoadObj(const std::filesystem::path& path, Mesh& out, std::string& error) {
    out = Mesh{};

    if (!CheckInputFile(path, error)) return false;

    rapidobj::Result result = rapidobj::ParseFile(path, rapidobj::MaterialLibrary::Ignore());
    if (result.error) {
        // NUL(\0)などの制御文字が混入したファイル(一部のエクスポータが末尾に出力する)への対策:
        // 全体を読み込んで NUL を除去し、ストリームとして解析し直す。
        std::ifstream ifs(path, std::ios::binary);
        if (ifs) {
            std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            data.erase(std::remove(data.begin(), data.end(), '\0'), data.end());
            std::istringstream iss(data);
            rapidobj::Result   retry = rapidobj::ParseStream(iss, rapidobj::MaterialLibrary::Ignore());
            if (!retry.error) result = std::move(retry);
        }
    }
    if (result.error) {
        error = "OBJ解析エラー: " + result.error.code.message();
        if (result.error.line_num != 0) {
            error += " (line " + std::to_string(result.error.line_num) + ")";
        }
        if (!result.error.line.empty()) {
            error += " 該当行: " + result.error.line;
        }
        return false;
    }

    // rapidobj::Triangulate は MaterialLibrary::Ignore() と併用すると material_ids が空で
    // クラッシュするため使わず、面をファン分割して三角形リストを自前で作る。
    std::vector<rapidobj::Index> tris;
    for (const auto& shape : result.shapes) {
        const auto& mesh = shape.mesh;
        size_t      off  = 0;
        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f) {
            const size_t n = mesh.num_face_vertices[f];
            if (off + n > mesh.indices.size()) break; // 不正データ防止
            for (size_t i = 1; i + 1 < n; ++i) {
                tris.push_back(mesh.indices[off]);
                tris.push_back(mesh.indices[off + i]);
                tris.push_back(mesh.indices[off + i + 1]);
            }
            off += n;
        }
    }
    if (tris.empty()) {
        error = "三角形がありません";
        return false;
    }

    const auto&  attr     = result.attributes;
    const size_t posCount = attr.positions.size() / 3;
    if (posCount == 0) {
        error = "頂点がありません";
        return false;
    }
    if (attr.texcoords.empty()) {
        error = "このOBJにはUV座標(vt)がありません。UV展開してからUV付きで書き出してください";
        return false;
    }

    bool needNormals = attr.normals.empty();
    for (const auto& idx : tris) {
        if (idx.texcoord_index < 0) {
            error = "UVを持たない頂点があります";
            return false;
        }
        if (idx.normal_index < 0) needNormals = true;
    }

    // 法線が無い(または一部欠けている)場合は位置ごとのスムース法線を生成
    std::vector<std::array<float, 3>> smooth;
    if (needNormals) {
        smooth.assign(posCount, {0.f, 0.f, 0.f});
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const float* p0 = &attr.positions[3 * tris[i + 0].position_index];
            const float* p1 = &attr.positions[3 * tris[i + 1].position_index];
            const float* p2 = &attr.positions[3 * tris[i + 2].position_index];
            float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            float n[3]  = {e1[1] * e2[2] - e1[2] * e2[1],
                           e1[2] * e2[0] - e1[0] * e2[2],
                           e1[0] * e2[1] - e1[1] * e2[0]}; // 面積重み付き
            for (int k = 0; k < 3; ++k) {
                auto& sn = smooth[tris[i + k].position_index];
                sn[0] += n[0];
                sn[1] += n[1];
                sn[2] += n[2];
            }
        }
        for (auto& s : smooth) Normalize3(s.data());
    }

    std::unordered_map<Key, uint32_t, KeyHash> map;
    {
        for (const auto& idx : tris) {
            Key k{idx.position_index, idx.texcoord_index, needNormals ? -1 : idx.normal_index};
            auto it = map.find(k);
            if (it == map.end()) {
                Vertex v{};
                v.px = attr.positions[3 * idx.position_index + 0];
                v.py = attr.positions[3 * idx.position_index + 1];
                v.pz = attr.positions[3 * idx.position_index + 2];
                if (needNormals) {
                    const auto& s = smooth[idx.position_index];
                    v.nx = s[0];
                    v.ny = s[1];
                    v.nz = s[2];
                } else {
                    float n[3] = {attr.normals[3 * idx.normal_index + 0],
                                  attr.normals[3 * idx.normal_index + 1],
                                  attr.normals[3 * idx.normal_index + 2]};
                    Normalize3(n);
                    v.nx = n[0];
                    v.ny = n[1];
                    v.nz = n[2];
                }
                v.u = attr.texcoords[2 * idx.texcoord_index + 0];
                v.v = attr.texcoords[2 * idx.texcoord_index + 1];

                uint32_t newIndex = static_cast<uint32_t>(out.vertices.size());
                out.vertices.push_back(v);
                it = map.emplace(k, newIndex).first;
            }
            out.indices.push_back(it->second);
        }
    }

    if (out.indices.empty()) {
        error = "三角形がありません";
        return false;
    }

    ComputeBounds(out);
    return true;
}

void ComputeBounds(Mesh& out) {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& v : out.vertices) {
        const float p[3] = {v.px, v.py, v.pz};
        for (int k = 0; k < 3; ++k) {
            mn[k] = std::min(mn[k], p[k]);
            mx[k] = std::max(mx[k], p[k]);
        }
    }
    float d2 = 0;
    for (int k = 0; k < 3; ++k) {
        out.center[k] = 0.5f * (mn[k] + mx[k]);
        float d       = mx[k] - mn[k];
        d2 += d * d;
    }
    out.radius = std::max(0.5f * std::sqrt(d2), 1e-6f);
}

ModelFormat ModelFormatFromPath(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (ext == L".obj") return ModelFormat::Obj;
    if (ext == L".gltf" || ext == L".glb") return ModelFormat::Gltf;
    if (ext == L".fbx") return ModelFormat::Fbx;
    return ModelFormat::Unknown;
}

bool LoadMesh(const std::filesystem::path& path, Mesh& out, std::string& error) {
    switch (ModelFormatFromPath(path)) {
    case ModelFormat::Obj: return LoadObj(path, out, error);
    case ModelFormat::Gltf: return LoadGltf(path, out, error);
    case ModelFormat::Fbx: return LoadFbx(path, out, error);
    default: error = "未対応の拡張子です（対応: .obj / .gltf / .glb / .fbx）"; return false;
    }
}

void RecomputeSmoothNormals(Mesh& m) {
    if (m.vertices.empty() || m.indices.size() < 3) return;

    // 位置が(ほぼ)同じ頂点を同一視する
    std::vector<uint32_t> wid;
    const uint32_t        wcount = WeldVertexIds(m, wid);

    std::vector<std::array<double, 3>> acc(wcount, std::array<double, 3>{0.0, 0.0, 0.0});
    auto sub3  = [](const Vertex& a, const Vertex& b) { return std::array<double, 3>{double(a.px) - b.px, double(a.py) - b.py, double(a.pz) - b.pz}; };
    auto len3  = [](const std::array<double, 3>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    auto dot3  = [](const std::array<double, 3>& a, const std::array<double, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };

    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const uint32_t i3[3] = {m.indices[t], m.indices[t + 1], m.indices[t + 2]};
        const Vertex*  p[3]  = {&m.vertices[i3[0]], &m.vertices[i3[1]], &m.vertices[i3[2]]};

        auto e01 = sub3(*p[1], *p[0]);
        auto e02 = sub3(*p[2], *p[0]);
        std::array<double, 3> fn = {e01[1] * e02[2] - e01[2] * e02[1],
                                    e01[2] * e02[0] - e01[0] * e02[2],
                                    e01[0] * e02[1] - e01[1] * e02[0]};
        const double fl = len3(fn);
        if (fl < 1e-30) continue; // 退化三角形
        for (auto& c : fn) c /= fl;

        for (int c = 0; c < 3; ++c) {
            auto ea = sub3(*p[(c + 1) % 3], *p[c]);
            auto eb = sub3(*p[(c + 2) % 3], *p[c]);
            const double la = len3(ea), lb = len3(eb);
            if (la < 1e-30 || lb < 1e-30) continue;
            const double angle = std::acos(std::clamp(dot3(ea, eb) / (la * lb), -1.0, 1.0)); // 頂角で重み付け
            auto&        a     = acc[wid[i3[c]]];
            a[0] += fn[0] * angle;
            a[1] += fn[1] * angle;
            a[2] += fn[2] * angle;
        }
    }

    for (size_t i = 0; i < m.vertices.size(); ++i) {
        auto&        a = acc[wid[i]];
        const double l = len3(a);
        if (l < 1e-30) continue; // 元の法線を残す
        m.vertices[i].nx = static_cast<float>(a[0] / l);
        m.vertices[i].ny = static_cast<float>(a[1] / l);
        m.vertices[i].nz = static_cast<float>(a[2] / l);
    }
}
