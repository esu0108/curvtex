#pragma once
#include <filesystem>
#include <string>

#include "mesh/Mesh.h"

// ローダー(OBJ / glTF / FBX)で共通の組み立て処理。

// 入力ファイルが存在する通常ファイルか確認する。無ければ error を設定して false。
bool CheckInputFile(const std::filesystem::path& path, std::string& error);

// ワールド空間へ変換済みの1パーツ(頂点 + 三角形リスト)を out へ結合する。
//   mirrored   : 変換が鏡像(行列式が負)なら true。三角形の巻き順を入れ替えて表裏を保つ。
//   hasNormals : false ならパーツ単位でスムース法線を生成する。
void AppendPart(Mesh& out, Mesh part, bool mirrored, bool hasNormals);

// 全パーツを結合し終えた後の仕上げ。三角形が無ければ error を設定して false、あれば境界球を計算する。
//   skippedForMissingUv : UV が無いために読み飛ばしたパーツがあったか
//   missingUvMessage    : その場合に出すメッセージ(形式ごとに文言が違う)
bool FinalizeMesh(Mesh& out, std::string& error, bool skippedForMissingUv, const char* missingUvMessage);
