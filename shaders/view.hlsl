// 3Dビュー: モデルに曲率テクスチャを貼って表示する
cbuffer Constants : register(b0) {
    row_major float4x4 mvp;  // モデル*ビュー*射影 (行ベクトル規約, DirectXMath と同じ)
    row_major float4x4 view; // 法線をビュー空間へ回す用 (上左3x3のみ使用)
    float              useLighting; // 0: テクスチャそのまま, 1: ライティングあり
    float3             pad;
};

Texture2D    tex : register(t0);
SamplerState smp : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};
struct VSOut {
    float4 sv  : SV_POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    o.sv  = mul(float4(i.pos, 1.0), mvp);
    o.nrm = mul(i.nrm, (float3x3)view); // モデル行列は平行移動+一様スケールのみなので回転は view だけ
    o.uv  = i.uv;
    return o;
}

float4 PSMain(VSOut i, bool front : SV_IsFrontFace) : SV_Target {
    // UV の v は上向き(v=1 がテクスチャ最上行)なので反転してサンプルする
    const float3 albedo = tex.Sample(smp, float2(i.uv.x, 1.0 - i.uv.y)).rgb;

    float3 n = normalize(i.nrm);
    if (!front) n = -n; // 両面描画: 裏面は法線を反転

    const float3 L     = normalize(float3(0.35, 0.55, 0.75)); // ビュー空間のライト方向(カメラ寄り)
    const float  d     = saturate(dot(n, L));
    const float  shade = lerp(1.0, 0.30 + 0.70 * d, useLighting);
    return float4(albedo * shade, 1.0);
}
