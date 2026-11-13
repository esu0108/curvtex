cbuffer CB : register(b0) {
    uint  texW;
    uint  texH;
    float invRange; // 1 / 最大曲率
    uint  mode;     // 0: 符号付き(0.5=平坦, 凸>0.5), 1: 絶対値
    uint  radius;   // 差分のサンプル幅(テクセル)。大きいほど滑らか
};

Texture2D<float4> gNormal   : register(t0);
Texture2D<float4> gPosition : register(t1);

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

float4 PSMain(VSOut i) : SV_Target {
    int2   c = int2(i.pos.xy);
    float4 a = gNormal.Load(int3(c, 0));
    if (a.w < 0.5) return float4(0, 0, 0, 1); // モデルが無い領域

    float3 n = a.xyz;
    float3 p = gPosition.Load(int3(c, 0)).xyz;

    float k = 0.5 * (DirCurvature(c, int2(1, 0), n, p) + DirCurvature(c, int2(0, 1), n, p));

    float v = (mode == 0) ? saturate(0.5 + 0.5 * k * invRange)
                          : saturate(abs(k) * invRange);
    return float4(v, v, v, 1);
}
