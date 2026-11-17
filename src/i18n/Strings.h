#pragma once
#include <cstddef>
#include <string>
#include <vector>

// UI 文字列の多言語化。
//   - 英語の文言だけをここ (I18N_STRINGS) に埋め込む。どの言語のファイルにも無い文字列は、この英語になる。
//   - 各言語の文言は、実行ファイルの隣の assets/lang フォルダにある assets/lang/<言語コード>.lang から起動時に読み込む。
//     言語を足すときは en.lang をコピーして訳すだけで、再ビルドは要らない (書式は en.lang の冒頭を参照)。
//   - 文字列を足すときは、ここに ID と英語を足し、assets/lang/*.lang へ同じキーで訳を足す (足し忘れた言語は英語になる)。
// printf の指定子 (%u %zu %s など) は、全言語で英語と同じ順序で並べること。違う訳は読み込み時に無視される。

// clang-format off
#define I18N_STRINGS(X) \
    X(Empty, "") \
    X(MenuFile, "File") \
    X(MenuOpen, "Open...") \
    X(MenuSave, "Save...") \
    X(MenuExit, "Exit") \
    X(MenuView, "View") \
    X(MenuAntiAliasing, "Anti-aliasing") \
    X(MenuAnisotropic, "Anisotropic filtering") \
    X(OptOff, "Off") \
    X(MenuLanguage, "Language") \
    X(SecModel, "Model") \
    X(HintLoadModel, "Load a model with File > Open, or by dragging and dropping it onto the window.") \
    X(ChkSmoothNormals, "Recompute smooth normals") \
    X(HintSmoothNormals, "Welds vertices at the same position to smooth the normals.") \
    X(Working, "Processing...") \
    X(SecOutput, "Output") \
    X(SecTexSize, "Texture size") \
    X(Width, "Width") \
    X(Height, "Height") \
    X(ApplySize, "Apply size") \
    X(CurrentSize, "Current: %u x %u") \
    X(SecCurvature, "Curvature") \
    X(ModeSigned, "Signed") \
    X(ModeAbsolute, "Absolute") \
    X(HintMode, "Signed: 0.5 = flat, convex > 0.5, concave < 0.5 / Absolute: 0 = flat") \
    X(Smoothing, "Smoothing (iterations)") \
    X(HintSmoothing, "How many times each vertex's curvature is averaged with its neighbors. Higher is smoother (0 = unchanged).") \
    X(Padding, "Padding (texels)") \
    X(HintPadding, "Extends the nearest values outward from UV islands to prevent dark seams.") \
    X(MaxCurvature, "Max curvature (1/unit length)") \
    X(ResetDefault, "Reset to default") \
    X(HintMaxCurvature, "The output saturates to 0/1 at this curvature. A sphere of radius R has curvature 1/R.") \
    X(TabView3D, "3D View") \
    X(TabTexture, "Texture") \
    X(Empty3D, "Load a model and it will appear here with the curvature texture applied.") \
    X(EmptyTexture, "Load a model and the curvature texture will appear here.") \
    X(Lighting, "Lighting") \
    X(ResetCamera, "Reset camera") \
    X(CameraHelp, "Left-drag: rotate / Right- or middle-drag: pan / Wheel: zoom") \
    X(ResetView, "Reset view") \
    X(TexLinear, "Linear interpolation") \
    X(TextureHelp, "Wheel: zoom / Drag: pan / Double-click: reset") \
    X(LabelPixel, "Pixel") \
    X(LabelValue, "Display value") \
    X(LabelCurvature, "Curvature") \
    X(SecParts, "Bake targets") \
    X(LabelMeshes, "Meshes") \
    X(LabelMaterials, "Materials") \
    X(LabelUvChannel, "UV channel") \
    X(BtnAll, "All") \
    X(BtnNone, "None") \
    X(BtnApplySelection, "Apply selection") \
    X(NoMaterial, "(no material)") \
    X(HintParts, "Only parts that match both a checked mesh and a checked material are baked. Press \"Apply selection\" after changing them. OBJ materials are not loaded, so OBJ files only offer mesh (o / g) selection.") \
    X(ErrSelEmpty, "No parts are selected. Choose at least one mesh and one material.") \
    X(ErrSelNoUv, "None of the selected parts has the chosen UV channel.") \
    X(StNoModel, "No model loaded") \
    X(StLoading, "Loading...") \
    X(StLoaded, "Loaded: %zu vertices / %zu triangles / radius %.4g") \
    X(StGpuFail, "Failed to upload to the GPU: ") \
    X(StRemeshFail, "Failed to update the mesh: ") \
    X(StLoadFail, "Load failed: ") \
    X(StDropBusy, "Ignored the dropped file because a model is being loaded") \
    X(StDropUnsupported, "Unsupported file (supported: .obj / .gltf / .glb / .fbx)") \
    X(StSaving, "Saving: ") \
    X(StSaved, "Saved: ") \
    X(StSaveFail, "Save failed: ") \
    X(StReadbackFail, "Save failed (could not read back the result)") \
    X(ExceptionPrefix, "Exception: ") \
    X(DlgOpenTitle, "Open model") \
    X(DlgSaveTitle, "Save image") \
    X(DlgFilter3D, "3D models (*.obj;*.gltf;*.glb;*.fbx)") \
    X(DlgFilterAll, "All files (*.*)") \
    X(ErrTgaSize, "TGA width and height must be 65535 or less") \
    X(ErrOpenFile, "Cannot open the file") \
    X(ErrWriteFail, "Write failed") \
    X(ErrPngFmt, "PNG save failed (%s, HRESULT=0x%08X)") \
    X(WicFactory, "WIC factory") \
    X(WicStream, "create stream") \
    X(WicOpenFile, "open file") \
    X(WicEncoderCreate, "create encoder") \
    X(WicEncoderInit, "initialize encoder") \
    X(WicFrameCreate, "create frame") \
    X(WicFrameInit, "initialize frame") \
    X(WicSetSize, "set size") \
    X(WicPixelFormat, "pixel format") \
    X(WicWritePixels, "write pixels") \
    X(WicCommitFrame, "commit frame") \
    X(WicCommitFile, "commit file") \
    X(ErrPngNo24bit, "The PNG encoder does not support 24-bit BGR") \
    X(ErrNoImage, "No image data") \
    X(ErrBmpWrite, "BMP write failed") \
    X(ErrImageExt, "Unsupported extension (supported: .bmp / .png / .tga)") \
    X(ErrFileNotFound, "File not found (please check the path)") \
    X(ErrObjParse, "OBJ parse error: ") \
    X(ErrObjLine, " Line: ") \
    X(ErrNoTriangles, "There are no triangles") \
    X(ErrNoVertices, "There are no vertices") \
    X(ErrObjNoUv, "This OBJ has no UV coordinates (vt). Unwrap the UVs and export again with UVs.") \
    X(ErrVertexNoUv, "Some vertices have no UV coordinates") \
    X(ErrModelExt, "Unsupported extension (supported: .obj / .gltf / .glb / .fbx)") \
    X(ErrFbxParse, "FBX parse error: ") \
    X(ErrFbxNoUv, "This FBX has no UV coordinates. Unwrap the UVs before exporting.") \
    X(ErrGltfLoad, "glTF load error: ") \
    X(ErrGltfParse, "glTF parse error: ") \
    X(ErrGltfNoUv, "This glTF has no UV coordinates (TEXCOORD_0). Unwrap the UVs before exporting.")

// clang-format on

#define I18N_DECLARE_ID(id, en) id,
enum class Str : int { I18N_STRINGS(I18N_DECLARE_ID) Count };
#undef I18N_DECLARE_ID

// フォントの種類。言語ごとに、どの文字種のフォントを先頭にするかを決める (lang ファイルの @font)。
enum class FontKind { Ja, Zh, ZhHant, Ko, Th, Latin };

// 読み込めた言語の数 (組み込みの英語があるので 1 以上)。言語の番号は 0 ～ LangCount()-1 で、メニューの並び順。
int LangCount();

// 現在の言語の番号 (スレッドセーフ)。初回は OS の表示言語に合う言語、無ければ英語。
int  GetLang();
void SetLang(int lang);

// 言語名 (その言語自身の表記。言語メニュー用) とフォントの種類
const char* LangNativeName(int lang);
FontKind    LangFontKind(int lang);

// assets/lang の読み込みで見つかった問題 (英語の文面。見つからない・書式が違う・未知のキーなど)。起動後は変わらない。
const std::vector<std::string>& LangDiagnostics();

// 現在の言語の文字列 (UTF-8)。ワーカースレッドからも呼べる。その言語に無ければ英語。
const char* Tr(Str id);
const char* Tr(int lang, Str id);

// ステータス表示用のメッセージ。言語を切り替えたときに表示も追従するよう、文字列ではなく ID + 引数で持つ。
struct StatusMsg {
    Str         id = Str::Empty;
    std::string detail; // ID の文言の後ろに付ける詳細 (エラー本文・パスなど)
    // id == Str::StLoaded のときだけ使う
    size_t verts  = 0;
    size_t tris   = 0;
    double radius = 0.0;
};

// 現在の言語で表示用の文字列にする。
std::string FormatStatus(const StatusMsg& m);
