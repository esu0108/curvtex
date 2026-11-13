struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};
struct VSOut {
    float4 sv   : SV_POSITION;
    float3 nrm  : NORMAL;
    float3 wpos : TEXCOORD0;
};

// 頂点のUVを(クリップ空間の)POSITIONとして出力する。
// UVの v は上向きなので、v=1 がテクスチャの最上行になる。
VSOut VSMain(VSIn i) {
    VSOut o;
    o.sv   = float4(i.uv.x * 2.0 - 1.0, i.uv.y * 2.0 - 1.0, 0.0, 1.0);
    o.nrm  = i.nrm;
    o.wpos = i.pos;
    return o;
}

struct PSOut {
    float4 normal   : SV_Target0; // xyz = 法線, w = カバレッジ(1)
    float4 position : SV_Target1; // xyz = オブジェクト空間位置(曲率のスケール補正用)
};

PSOut PSMain(VSOut i) {
    PSOut o;
    o.normal   = float4(normalize(i.nrm), 1.0);
    o.position = float4(i.wpos, 1.0);
    return o;
}
