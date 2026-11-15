// 頂点ごとの曲率 (GPU コンピュート)。
// 隣接頂点ごとの曲率  k_ij = (n_j - n_i)・(p_j - p_i) / |p_j - p_i|^2  を平均する。
//   - 法線が辺に沿ってどれだけ変化するか(法線の変化量 / 辺の長さ)。半径 R の球ではどの辺でも 1/R になる。
//   - 凸(法線が外へ開く)で正、凹で負。角度の近似を使わないので、粗いメッシュでも歪まない。
// 結果は 1/単位長 (メッシュの座標単位の逆数)。

StructuredBuffer<float3> gPositions : register(t0);
StructuredBuffer<float3> gNormals   : register(t1);
StructuredBuffer<uint>   gOffsets   : register(t2); // N+1 個 (CSR)
StructuredBuffer<uint>   gNeighbors : register(t3);
StructuredBuffer<float>  gCurvIn    : register(t4); // CSSmooth の入力

RWStructuredBuffer<float> gCurvOut : register(u0);

cbuffer CB : register(b0) {
    uint vertexCount;
};

[numthreads(256, 1, 1)]
void CSCurvature(uint3 id : SV_DispatchThreadID) {
    const uint v = id.x;
    if (v >= vertexCount) return;

    const float3 p = gPositions[v];
    const float3 n = gNormals[v];
    const uint   b = gOffsets[v];
    const uint   e = gOffsets[v + 1];

    float sum = 0.0;
    [loop]
    for (uint i = b; i < e; ++i) {
        const uint   j  = gNeighbors[i];
        const float3 d  = gPositions[j] - p;
        const float  d2 = dot(d, d);
        if (d2 > 1e-20) sum += dot(gNormals[j] - n, d) / d2;
    }
    gCurvOut[v] = (e > b) ? sum / (float)(e - b) : 0.0;
}

// スムージング1回分: 自分の値と隣接の平均値を 1:1 で混ぜる (ラプラシアン平滑化)
[numthreads(256, 1, 1)]
void CSSmooth(uint3 id : SV_DispatchThreadID) {
    const uint v = id.x;
    if (v >= vertexCount) return;

    const uint  b = gOffsets[v];
    const uint  e = gOffsets[v + 1];
    float       c = gCurvIn[v];
    if (e > b) {
        float sum = 0.0;
        [loop]
        for (uint i = b; i < e; ++i) sum += gCurvIn[gNeighbors[i]];
        c = lerp(c, sum / (float)(e - b), 0.5);
    }
    gCurvOut[v] = c;
}
