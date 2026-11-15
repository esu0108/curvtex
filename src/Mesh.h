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

// 読み込める形式
enum class ModelFormat { Obj, Gltf, Fbx, Unknown };

// 拡張子(大文字小文字を区別しない)から形式を判定する。
ModelFormat ModelFormatFromPath(const std::filesystem::path& path);

// 拡張子(.obj / .gltf / .glb / .fbx)で読み込み関数を振り分ける。UV必須。
bool LoadMesh(const std::filesystem::path& path, Mesh& out, std::string& error);

// 個別のローダー。いずれも UV 必須で、法線が無ければスムース法線を生成する。
bool LoadObj(const std::filesystem::path& path, Mesh& out, std::string& error);  // rapidobj
bool LoadGltf(const std::filesystem::path& path, Mesh& out, std::string& error); // fastgltf (.gltf / .glb)
bool LoadFbx(const std::filesystem::path& path, Mesh& out, std::string& error);  // ufbx

// vertices から center / radius を計算する。
void ComputeBounds(Mesh& mesh);

// 同じ位置の頂点(UV継ぎ目・ハードエッジで分割された頂点)を溶接した上で、
// 角度重み付きのスムース法線を再計算して vertices[].n に書き込む。
void RecomputeSmoothNormals(Mesh& mesh);
