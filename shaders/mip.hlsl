// ミップマップ生成: 1つ上のレベル(または曲率テクスチャ本体)をバイリニアで縮小して、次のレベルへ書く。
// 出力ピクセルの中心でサンプルすると、元の 2x2 テクセルのちょうど中央になるので、そのまま 2x2 の平均になる。

cbuffer CB : register(b0) {
    float2 invSize; // 出力レベルのサイズの逆数
};

Texture2D<float4> src : register(t0);
SamplerState      smp : register(s0); // バイリニア + CLAMP

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    return src.SampleLevel(smp, i.pos.xy * invSize, 0);
}
