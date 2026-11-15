// ジャンプフラッディング: モデルのあるテクセル(シード)から、各テクセルへ「最も近いシードの座標」を伝播させる。
// 結果は R16G16_UINT テクスチャ(xy = シードのテクセル座標, 0xFFFF = 未確定)。
// パディングでは、モデルの無いテクセルがこの座標を使って最寄りのテクセルの曲率値を借りる。

cbuffer CB : register(b0) {
    uint texW;
    uint texH;
    int  stepSize; // ステップ幅(テクセル)。PSStep のみ使用
    uint pad;
};

Texture2D<float2> gCurv   : register(t0); // PSInit: カバレッジ(.y)の読み出し用
Texture2D<uint2>  gSeed   : register(t1); // PSStep: 前のパスの結果

static const uint kInvalid = 0xFFFF;

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// モデルのあるテクセルは自分自身がシード
uint2 PSInit(VSOut i) : SV_Target {
    const int2 c = int2(i.pos.xy);
    return (gCurv.Load(int3(c, 0)).y >= 0.5) ? uint2(c) : uint2(kInvalid, kInvalid);
}

// 周囲 8 方向 + 自分(stepSize 離れた位置)のシードのうち、最も近いものを採用する
uint2 PSStep(VSOut i) : SV_Target {
    const int2 c    = int2(i.pos.xy);
    uint2      best = uint2(kInvalid, kInvalid);
    int        bestD = 0x7fffffff;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            const int2 q = c + int2(dx, dy) * stepSize;
            if (q.x < 0 || q.y < 0 || q.x >= (int)texW || q.y >= (int)texH) continue;
            const uint2 s = gSeed.Load(int3(q, 0));
            if (s.x == kInvalid) continue;
            const int2 d  = int2(s) - c;
            const int  d2 = d.x * d.x + d.y * d.y;
            if (d2 < bestD) {
                bestD = d2;
                best  = s;
            }
        }
    }
    return best;
}
