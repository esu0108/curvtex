// 頂点ごとの曲率を、三角形の中で補間しながら UV 空間のテクスチャへ書き出す。
// 出力: x = 曲率(1/単位長), y = カバレッジ(1)。モデルの無いテクセルは (0,0) のまま。

StructuredBuffer<uint>  gNodeOfVertex : register(t0); // 頂点 → 曲率ノード (UV継ぎ目で複製された頂点は同じノード)
StructuredBuffer<float> gCurv         : register(t1); // ノードごとの曲率

struct VSIn {
    float3 pos : POSITION; // 使わない (入力レイアウトはメッシュ共通)
    float3 nrm : NORMAL;   // 使わない
    float2 uv  : TEXCOORD0;
    uint   id  : SV_VertexID; // インデックスバッファの値 = 頂点番号
};
struct VSOut {
    float4 sv : SV_POSITION;
    float  k  : TEXCOORD0;
};

// 頂点のUVを(クリップ空間の)POSITIONとして出力する。UVの v は上向きなので、v=1 がテクスチャの最上行になる。
VSOut VSMain(VSIn i) {
    VSOut o;
    o.sv = float4(i.uv.x * 2.0 - 1.0, i.uv.y * 2.0 - 1.0, 0.0, 1.0);
    o.k  = gCurv[gNodeOfVertex[i.id]];
    return o;
}

float2 PSMain(VSOut i) : SV_Target {
    return float2(i.k, 1.0);
}
