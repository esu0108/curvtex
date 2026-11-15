#pragma once
#include <cstdint>
#include <vector>

#include "mesh/Mesh.h"

// 位置が(ほぼ)同じ頂点を同一視して番号を振る。UV継ぎ目・ハードエッジで分割された頂点が同じ番号になる。
// ids[i] に頂点 i の番号(0 .. 戻り値-1)を書き込み、番号の総数を返す。
// 同一視する許容幅はメッシュの境界球半径の 1e-5 (mesh.radius が計算済みであること)。
uint32_t WeldVertexIds(const Mesh& mesh, std::vector<uint32_t>& ids);
