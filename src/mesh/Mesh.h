#pragma once
#include <array>
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

// 扱える UV チャンネルの最大数
constexpr int kMaxUvChannels = 8;

// モデルの1パーツ = 「1メッシュ(ノード)× 1マテリアル」の三角形の集まり。ワールド空間へ変換済み。
struct MeshPart {
    int                                     meshIndex     = 0; // ModelScene::meshNames の番号
    int                                     materialIndex = 0; // ModelScene::materialNames の番号
    Mesh                                    geo;               // 位置と法線 (Vertex の u, v は使わない)
    std::vector<std::vector<std::array<float, 2>>> uvs;        // uvs[チャンネル][頂点]。このパーツに無いチャンネルは空
};

// ファイルから読み込んだ全パーツ。どのパーツ・UV で曲率をベイクするかは ModelSelection で選ぶ。
struct ModelScene {
    std::vector<std::string> meshNames;     // 空文字なら名前なし
    std::vector<std::string> materialNames; // 同上。noMaterial の項目だけは表示側で「マテリアルなし」とする
    int                      noMaterial = -1; // マテリアルを持たないパーツ用の項目の番号 (無ければ -1)
    int                      uvCount    = 1;  // いずれかのパーツが持つ UV チャンネルの最大数
    std::vector<MeshPart>    parts;
};

// ベイク対象の選択。meshes / materials は 1 = 有効。両方が有効なパーツだけが対象になる。
struct ModelSelection {
    std::vector<uint8_t> meshes;
    std::vector<uint8_t> materials;
    int                  uvChannel = 0;
    bool                 operator==(const ModelSelection&) const = default;
};

// 全メッシュ・全マテリアルを選び、UV チャンネルは 0 にした選択を作る。
ModelSelection SelectAll(const ModelScene& scene);

// 読み込める形式
enum class ModelFormat { Obj, Gltf, Fbx, Unknown };

// 拡張子(大文字小文字を区別しない)から形式を判定する。
ModelFormat ModelFormatFromPath(const std::filesystem::path& path);

// 拡張子(.obj / .gltf / .glb / .fbx)で読み込み関数を振り分ける。UV必須 (UV0 の無いパーツは読み飛ばす)。
bool LoadScene(const std::filesystem::path& path, ModelScene& out, std::string& error);

// 選択したパーツ・UV チャンネルから、ベイク用の1つのメッシュを作る (法線はローダーが作ったまま。境界球は計算済み)。
// 選択が空、または選んだ UV チャンネルを持つパーツが無ければ error を設定して false。
bool BuildMesh(const ModelScene& scene, const ModelSelection& sel, Mesh& out, std::string& error);

// 個別のローダー。いずれも UV 必須で、法線が無ければスムース法線を生成する。
bool LoadObj(const std::filesystem::path& path, ModelScene& out, std::string& error);  // rapidobj (マテリアルは読まない)
bool LoadGltf(const std::filesystem::path& path, ModelScene& out, std::string& error); // fastgltf (.gltf / .glb)
bool LoadFbx(const std::filesystem::path& path, ModelScene& out, std::string& error);  // ufbx

// vertices から center / radius を計算する。
void ComputeBounds(Mesh& mesh);

// 同じ位置の頂点(UV継ぎ目・ハードエッジで分割された頂点)を溶接した上で、
// 角度重み付きのスムース法線を再計算して vertices[].n に書き込む。
void RecomputeSmoothNormals(Mesh& mesh);
