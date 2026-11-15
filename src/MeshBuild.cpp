#include "MeshBuild.h"

#include <utility>

bool CheckInputFile(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = "ファイルが見つかりません（パスを確認してください）";
        return false;
    }
    return true;
}

void AppendPart(Mesh& out, Mesh part, bool mirrored, bool hasNormals) {
    if (part.indices.size() < 3 || part.vertices.empty()) return;

    if (mirrored) {
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) std::swap(part.indices[i + 1], part.indices[i + 2]);
    }
    if (!hasNormals) { // 法線が無いパーツはスムース法線を生成 (溶接の許容幅にパーツ自身の半径を使う)
        ComputeBounds(part);
        RecomputeSmoothNormals(part);
    }

    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
    out.vertices.insert(out.vertices.end(), part.vertices.begin(), part.vertices.end());
    out.indices.reserve(out.indices.size() + part.indices.size());
    for (uint32_t i : part.indices) out.indices.push_back(base + i);
}

bool FinalizeMesh(Mesh& out, std::string& error, bool skippedForMissingUv, const char* missingUvMessage) {
    if (out.indices.empty()) {
        error = skippedForMissingUv ? missingUvMessage : "三角形がありません";
        return false;
    }
    ComputeBounds(out);
    return true;
}
