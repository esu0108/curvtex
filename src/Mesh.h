#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct Vertex {
    float px, py, pz; // POSITION
    float nx, ny, nz; // NORMAL
    float u, v;       // TEXCOORD0
};

struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    float center[3] = {0, 0, 0};
    float radius    = 1.0f; // バウンディング球半径(曲率レンジの初期値決定に使う)
};

// rapidobj で .obj を読み込む。UV必須。法線が無ければスムース法線を生成する。
bool LoadObj(const std::filesystem::path& path, Mesh& out, std::string& error);

// 同じ位置の頂点(UV継ぎ目・ハードエッジで分割された頂点)を溶接した上で、
// 角度重み付きのスムース法線を再計算して vertices[].n に書き込む。
void RecomputeSmoothNormals(Mesh& mesh);
