// ImGui 用の頂点・ピクセルシェーダー (imgui_impl_dx12.cpp 内蔵のものと同じ処理)。
// ImGui のバックエンドはサンプラーが線形補間に固定なので、テクスチャを最近傍で描きたい画像だけ、
// これと「点サンプラーの静的サンプラーを持つルートシグネチャ」(gpu/UiPointSampler.cpp) に差し替えて描く。

cbuffer Proj : register(b0) {
    float4x4 ProjectionMatrix;
};

struct VSIn {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = mul(ProjectionMatrix, float4(i.pos, 0.0, 1.0));
    o.col = i.col;
    o.uv  = i.uv;
    return o;
}

Texture2D<float4> tex : register(t0);
SamplerState      smp : register(s0); // 点(最近傍)サンプリング

float4 PSMain(VSOut i) : SV_Target {
    return i.col * tex.Sample(smp, i.uv);
}
