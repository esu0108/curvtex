# curvtex

.obj のUV座標を使って、曲率をテクスチャにベイクし、BMPで保存するツールです。

## ビルド（Windows / Visual Studio 2022 推奨）

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\curvtex.exe
```

初回の configure 時に FetchContent で次を取得します。

- Dear ImGui v1.93.0
- rapidobj v1.1（ヘッダオンリー）

## 使い方

1. 「モデル」: パスを入力するか「参照...」で .obj を選び「読み込み」
2. 「テクスチャサイズ」: 幅/高さ（16〜4096）を入力し「サイズを適用」
3. 「曲率」: 符号付き/絶対値の切り替えと最大曲率スライダーで見た目を調整（変更は即時反映）
4. 「画像保存 (BMP)」: パスを入力するか「参照...」で指定し「保存」（24bit BMP）

## 注意点

- UVが [0,1] の範囲外（タイリング）の部分は描画されません。
- UV島の境界では片側差分、孤立テクセルでは0になります。
- 法線が無い .obj は、位置ごとのスムース法線を自動生成します。
- UVが無い .obj は読み込めません。