# curvtex

.obj / .gltf / .glb / .fbx のUV座標を使って、曲率をテクスチャにベイクし、PNG / BMP / TGA で保存するツールです。

## ビルド（Windows / Visual Studio 2022 推奨）

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\curvtex.exe
```

初回の configure 時に FetchContent で次を取得します。

- Dear ImGui v1.93.0
- rapidobj v1.1（ヘッダオンリー）
- fastgltf v1.0.1（.gltf / .glb。simdjson / simdutf も自動取得）
- ufbx v0.23.1（.fbx。単一ソース）

## シェーダ

HLSL は `shaders/*.hlsl` にあり、ビルド時に `fxc`（Windows SDK）で事前コンパイルされ、
バイトコードが実行ファイルに埋め込まれます（実行時コンパイルはしません）。
`fxc.exe` が自動検出できない場合は `-DFXC_EXECUTABLE=<path>` を指定してください。

## 使い方

1. 「モデル」: パスを入力するか「参照...」で .obj / .gltf / .glb / .fbx を選び「読み込み」
2. 「テクスチャサイズ」: 幅/高さ（16〜4096）を入力し「サイズを適用」
3. 「曲率」: 符号付き/絶対値の切り替えと最大曲率スライダーで見た目を調整（変更は即時反映）
4. 「画像保存」: パスを入力するか「参照...」で指定し「保存」。拡張子で形式が決まります（.png / .bmp / .tga、いずれも24bit。拡張子なしは .png）。PNGは Windows 標準の WIC で書き出します。

## 注意点

- UVが [0,1] の範囲外（タイリング）の部分は描画されません。
- UV島の境界では片側差分、孤立テクセルでは0になります。
- 法線が無い .obj は、位置ごとのスムース法線を自動生成します。
- UVが無いモデルは読み込めません（glTFは TEXCOORD_0、FBXは最初のUVセットを使用）。
- glTF / FBX はシーン内の全メッシュにノード変換を適用して1つに結合します。
  UV が無いプリミティブは読み飛ばし、スキニング/モーフはバインドポーズのまま扱います。
- glTF の UV は v が下向きのため、本ツールの v 上向きに合わせて `v = 1 - v` へ変換します（OBJ / FBX はそのまま）。
- 圧縮メッシュ（Draco / meshopt）のglTFは未対応です。
- 曲率の単位はファイルの座標単位に依存します（FBXの単位変換は行いません）。