#include "mesh/MeshBuild.h"
#include "i18n/Strings.h"

#include <algorithm>
#include <utility>

bool CheckInputFile(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = Tr(Str::ErrFileNotFound);
        return false;
    }
    return true;
}

void AddPart(ModelScene& scene, MeshPart part, bool mirrored, bool hasNormals) {
    Mesh& g = part.geo;
    if (g.indices.size() < 3 || g.vertices.empty()) return;

    if (mirrored) {
        for (size_t i = 0; i + 2 < g.indices.size(); i += 3) std::swap(g.indices[i + 1], g.indices[i + 2]);
    }
    if (!hasNormals) { // 法線が無いパーツはスムース法線を生成 (溶接の許容幅にパーツ自身の半径を使う)
        ComputeBounds(g);
        RecomputeSmoothNormals(g);
    }
    scene.parts.push_back(std::move(part));
}

bool FinalizeScene(ModelScene& scene, std::string& error, bool skippedForMissingUv, const char* missingUvMessage) {
    if (scene.parts.empty()) {
        error = skippedForMissingUv ? missingUvMessage : Tr(Str::ErrNoTriangles);
        return false;
    }

    // 使われているマテリアルだけを、使われた順に並べ直す。無いパーツは末尾の専用項目へ。
    const int                oldCount = static_cast<int>(scene.materialNames.size());
    std::vector<int>         remap(oldCount, -1);
    std::vector<std::string> names;
    bool                     hasNone = false;
    for (const MeshPart& p : scene.parts) {
        const int m = p.materialIndex;
        if (m < 0 || m >= oldCount) {
            hasNone = true;
        } else if (remap[m] < 0) {
            remap[m] = static_cast<int>(names.size());
            names.push_back(scene.materialNames[m]);
        }
    }
    scene.noMaterial = -1;
    if (hasNone) {
        scene.noMaterial = static_cast<int>(names.size());
        names.emplace_back();
    }
    int uvCount = 1;
    for (MeshPart& p : scene.parts) {
        const int m     = p.materialIndex;
        p.materialIndex = (m < 0 || m >= oldCount) ? scene.noMaterial : remap[m];
        uvCount         = std::max(uvCount, static_cast<int>(p.uvs.size()));
    }
    scene.materialNames = std::move(names);
    scene.uvCount       = uvCount;
    return true;
}
