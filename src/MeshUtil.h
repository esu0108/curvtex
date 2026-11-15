#pragma once
#include <cmath>

#include "Mesh.h"

// ノード変換(アフィン変換)の共通処理。glTF / FBX ローダー双方で使う。
// 外部ライブラリの行列型に依存しないよう、列ベクトルを素の配列で保持する。
struct Affine {
    // c[0..2] = 基底ベクトル(列), c[3] = 平行移動
    double c[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};

    static void Cross(const double* a, const double* b, double* o) {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    }

    double Det() const {
        double x[3];
        Cross(c[1], c[2], x);
        return c[0][0] * x[0] + c[0][1] * x[1] + c[0][2] * x[2];
    }

    // 鏡像変換(行列式が負)だと三角形の向きが反転するので、呼び出し側で巻き順を入れ替える
    bool Mirrored() const { return Det() < 0.0; }

    void Point(const double in[3], float out[3]) const {
        for (int r = 0; r < 3; ++r) out[r] = static_cast<float>(c[0][r] * in[0] + c[1][r] * in[1] + c[2][r] * in[2] + c[3][r]);
    }

    // 法線は 逆転置行列 で変換する(非一様スケールでも正しい)。
    // 余因子行列 = det * 逆転置 なので、det の符号で割って向きを保つ。
    void Normal(const double in[3], float out[3]) const {
        double k0[3], k1[3], k2[3];
        Cross(c[1], c[2], k0);
        Cross(c[2], c[0], k1);
        Cross(c[0], c[1], k2);
        const double s = Det() < 0.0 ? -1.0 : 1.0;
        double       n[3];
        for (int r = 0; r < 3; ++r) n[r] = s * (k0[r] * in[0] + k1[r] * in[1] + k2[r] * in[2]);
        const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-20) {
            for (int r = 0; r < 3; ++r) out[r] = static_cast<float>(n[r] / l);
        } else {
            out[0] = 0;
            out[1] = 0;
            out[2] = 1;
        }
    }
};

// ローカル空間の位置・法線(任意)・UV から、変換後の頂点を作る。n が nullptr なら法線は 0 のまま。
inline Vertex MakeWorldVertex(const Affine& xf, const double p[3], const double* n, float u, float v) {
    Vertex x{};
    float  w[3];
    xf.Point(p, w);
    x.px = w[0];
    x.py = w[1];
    x.pz = w[2];
    if (n) {
        xf.Normal(n, w);
        x.nx = w[0];
        x.ny = w[1];
        x.nz = w[2];
    }
    x.u = u;
    x.v = v;
    return x;
}
