// 曲率(生の値)を表示用の 0..1 に変換して、結果テクスチャへ書き出す。
// モデルの無いテクセルは、ジャンプフラッディングで求めた最寄りのテクセルの値で埋める(パディング)。

cbuffer CB : register(b0) {
    uint  texW;
    uint  texH;
    float invRange; // 1 / 最大曲率
    uint  mode;     // 0: 符号付き(0.5=平坦, 凸>0.5), 1: 絶対値
    uint  pad;      // UV島の外側へ塗り広げる幅(テクセル)。0 で無効
};

Texture2D<float2> gCurv : register(t0); // x = 曲率, y = カバレッジ
Texture2D<uint2>  gSeed : register(t1); // ジャンプフラッディングの結果(最寄りのモデルのあるテクセルの座標)

static const uint kInvalidSeed = 0xFFFF;

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float Display(float k) {
    return (mode == 0) ? saturate(0.5 + 0.5 * k * invRange)
                       : saturate(abs(k) * invRange);
}

float4 PSMain(VSOut i) : SV_Target {
    const int2   c = int2(i.pos.xy);
    const float2 a = gCurv.Load(int3(c, 0));

    float v = 0.0;
    if (a.y >= 0.5) {
        v = Display(a.x);
    } else if (pad > 0) {
        const uint2 s = gSeed.Load(int3(c, 0));
        if (s.x != kInvalidSeed) {
            const int2 d = int2(s) - c;
            if (d.x * d.x + d.y * d.y <= (int)(pad * pad)) v = Display(gCurv.Load(int3(int2(s), 0)).x);
        }
    }
    return float4(v, v, v, 1.0);
}
