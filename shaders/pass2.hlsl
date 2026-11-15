cbuffer CB : register(b0) {
    uint  texW;
    uint  texH;
    float invRange; // 1 / 最大曲率
    uint  mode;     // 0: 符号付き(0.5=平坦, 凸>0.5), 1: 絶対値
    uint  radius;   // 差分のサンプル幅(テクセル)。大きいほど滑らか
    uint  pad;      // UV島の外側へ塗り広げる幅(テクセル)。0 で無効
    float invMeshRadius; // 1 / メッシュの境界球半径 (位置は半径で正規化されているので、曲率を元の単位へ戻す)
};

Texture2D<float4> gNormal   : register(t0);
Texture2D<float4> gPosition : register(t1);
Texture2D<uint2>  gSeed     : register(t2); // ジャンプフラッディングの結果(最寄りのモデルのあるテクセルの座標)

static const uint kInvalidSeed = 0xFFFF;

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

bool Fetch(int2 p, out float3 n, out float3 w) {
    n = 0; w = 0;
    if (p.x < 0 || p.y < 0 || p.x >= (int)texW || p.y >= (int)texH) return false;
    float4 a = gNormal.Load(int3(p, 0));
    if (a.w < 0.5) return false;
    n = a.xyz;
    w = gPosition.Load(int3(p, 0)).xyz;
    return true;
}

// 方向 d に沿った曲率 ≈ (Δn · Δp) / |Δp|^2  (法線の変化量 / 表面上の移動量)
float DirCurvature(int2 c, int2 d, float3 n0, float3 p0) {
    // 補間した頂点法線は三角形ごとに線形なので、1テクセル差分だと曲率が三角形ごとに一定(階段状)になる。
    // サンプル幅を広げて複数の三角形にまたがって平均する。端では幅を縮めて有効なテクセルを探す。
    [loop]
    for (int r = (int)radius; r >= 1; --r) {
        float3 nA, pA, nB, pB;
        bool a = Fetch(c - d * r, nA, pA);
        bool b = Fetch(c + d * r, nB, pB);
        if (!a && !b) continue;
        if (!a) { nA = n0; pA = p0; }
        if (!b) { nB = n0; pB = p0; }
        float3 dn = nB - nA;
        float3 dp = pB - pA;
        float  l2 = dot(dp, dp);
        return (l2 > 1e-20) ? dot(dn, dp) / l2 : 0.0;
    }
    return 0.0;
}

// テクセル c の曲率を 0..1 に正規化して返す
float CurvatureValue(int2 c, float3 n, float3 p) {
    // DirCurvature は正規化した位置で計算しているので、半径で割って 1/単位長 に戻す
    float k = 0.5 * (DirCurvature(c, int2(1, 0), n, p) + DirCurvature(c, int2(0, 1), n, p)) * invMeshRadius;
    return (mode == 0) ? saturate(0.5 + 0.5 * k * invRange)
                       : saturate(abs(k) * invRange);
}

float4 PSMain(VSOut i) : SV_Target {
    int2   c = int2(i.pos.xy);
    float4 a = gNormal.Load(int3(c, 0));

    if (a.w < 0.5) {
        // モデルが無い領域: pad > 0 なら、ジャンプフラッディングで求めた最寄りのテクセルの値で埋める
        // (UV島の境界の滲み/継ぎ目対策)。pad 以内に無ければ黒。
        if (pad > 0) {
            const uint2 s = gSeed.Load(int3(c, 0));
            if (s.x != kInvalidSeed) {
                const int2 d = int2(s) - c;
                if (d.x * d.x + d.y * d.y <= (int)(pad * pad)) {
                    const int2   q  = int2(s);
                    const float3 nq = gNormal.Load(int3(q, 0)).xyz;
                    const float3 pq = gPosition.Load(int3(q, 0)).xyz;
                    const float  v  = CurvatureValue(q, nq, pq);
                    return float4(v, v, v, 1);
                }
            }
        }
        return float4(0, 0, 0, 1);
    }

    float v = CurvatureValue(c, a.xyz, gPosition.Load(int3(c, 0)).xyz);
    return float4(v, v, v, 1);
}
