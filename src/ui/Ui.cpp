#include "ui/Ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "i18n/Strings.h"
#include "gpu/UiPointSampler.h"
#include "imgui.h"

namespace {

// 表示文字列は言語で変わるので、ID は "###" 以降で固定する (言語を切り替えてもタブの選択状態などが保たれる)
std::string IdLabel(Str text, const char* id) {
    return std::string(Tr(text)) + "###" + id;
}

// TextDisabled は折り返さないので、長い説明文が領域からはみ出す。折り返し付きで薄い色の文字を出す。
void TextDisabledWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// 「オフ / 2x / 4x ...」の選択肢を並べる。values[0] は 1 (オフ)。supported が false の項目は選べない。
template <class Supported>
void DrawMultiplierItems(int& current, std::initializer_list<int> values, Supported supported) {
    for (int v : values) {
        char label[16];
        if (v <= 1) std::snprintf(label, sizeof(label), "%s", Tr(Str::OptOff));
        else std::snprintf(label, sizeof(label), "%dx", v);
        if (ImGui::MenuItem(label, nullptr, current == v, supported(v))) current = v;
    }
}

// ---- メニューバー ----
void DrawMenuBar(AppState& s, const ViewPass& view, Actions& act) {
    const bool canOpen = !s.loading;
    const bool canSave = s.hasBaked && !s.saving;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(IdLabel(Str::MenuFile, "menuFile").c_str())) {
            if (ImGui::MenuItem(Tr(Str::MenuOpen), "Ctrl+O", false, canOpen)) act.openFile = true;
            if (ImGui::MenuItem(Tr(Str::MenuSave), "Ctrl+S", false, canSave)) act.saveFile = true;
            ImGui::Separator();
            if (ImGui::MenuItem(Tr(Str::MenuExit), "Alt+F4")) act.exitApp = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(IdLabel(Str::MenuView, "menuView").c_str())) {
            if (ImGui::BeginMenu(Tr(Str::MenuLanguage))) {
                const int cur = GetLang();
                // 言語名は、その言語自身の表記で出す (どの言語で動いていても選べるように)
                for (int i = 0; i < LangCount(); ++i) {
                    ImGui::PushID(i); // 同じ名前の言語があっても区別できるように
                    if (ImGui::MenuItem(LangNativeName(i), nullptr, cur == i)) SetLang(i);
                    ImGui::PopID();
                }
                // lang フォルダの読み込みで問題があったときだけ、内容を出す (英語の文面)
                const auto& diag = LangDiagnostics();
                if (!diag.empty()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Language file problems (%d)", static_cast<int>(diag.size()));
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        for (const std::string& d : diag) ImGui::TextUnformatted(d.c_str());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            // 以下は 3D ビューの描画品質
            if (ImGui::BeginMenu(IdLabel(Str::MenuAntiAliasing, "menuAA").c_str())) {
                DrawMultiplierItems(s.aaSamples, {1, 2, 4, 8}, [&](int n) { return view.SupportsMsaa(static_cast<UINT>(n)); });
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(IdLabel(Str::MenuAnisotropic, "menuAniso").c_str())) {
                DrawMultiplierItems(s.anisotropy, {1, 2, 4, 8, 16}, [](int) { return true; });
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ショートカット (メニューの表示とは別に、ここで受け付ける)
    if (canOpen && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) act.openFile = true;
    if (canSave && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) act.saveFile = true;
}

// 名前付きのチェックボックスの一覧 (全選択 / 全解除ボタン付き)。名前が空の項目は「#番号」、noneIndex の項目は「マテリアルなし」と表示する。
void DrawCheckList(Str title, const char* id, const std::vector<std::string>& names, int noneIndex, std::vector<uint8_t>& flags) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(Tr(title));
    ImGui::SameLine();
    if (ImGui::SmallButton(IdLabel(Str::BtnAll, "all").c_str())) std::fill(flags.begin(), flags.end(), uint8_t{1});
    ImGui::SameLine();
    if (ImGui::SmallButton(IdLabel(Str::BtnNone, "none").c_str())) std::fill(flags.begin(), flags.end(), uint8_t{0});

    const size_t rows = std::min<size_t>(names.size(), 8); // それ以上はスクロール
    const float  h    = ImGui::GetFrameHeightWithSpacing() * static_cast<float>(rows) + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGui::BeginChild("list", ImVec2(-FLT_MIN, h), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < names.size() && i < flags.size(); ++i) {
        std::string label;
        if (static_cast<int>(i) == noneIndex) label = Tr(Str::NoMaterial);
        else if (names[i].empty()) label = "#" + std::to_string(i);
        else label = names[i];

        ImGui::PushID(static_cast<int>(i)); // 同じ名前の項目があっても区別できるように
        bool on = flags[i] != 0;
        if (ImGui::Checkbox(label.c_str(), &on)) flags[i] = on ? 1 : 0;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// ベイク対象の選択 (メッシュ / マテリアル / UV チャンネル)。選択肢が 1 つしかない項目は出さない。
void DrawPartsPanel(AppState& s, Actions& act) {
    if (!s.scene) return;
    const ModelScene& sc       = *s.scene;
    const bool        showMesh = sc.meshNames.size() > 1 && s.sel.meshes.size() == sc.meshNames.size();
    const bool        showMat  = sc.materialNames.size() > 1 && s.sel.materials.size() == sc.materialNames.size();
    const bool        showUv   = sc.uvCount > 1;
    if (!showMesh && !showMat && !showUv) return;

    ImGui::SeparatorText(Tr(Str::SecParts));
    ImGui::BeginDisabled(s.loading);
    if (showMesh) DrawCheckList(Str::LabelMeshes, "meshes", sc.meshNames, -1, s.sel.meshes);
    if (showMat) DrawCheckList(Str::LabelMaterials, "materials", sc.materialNames, sc.noMaterial, s.sel.materials);
    if (showUv) {
        ImGui::TextUnformatted(Tr(Str::LabelUvChannel));
        char cur[16];
        std::snprintf(cur, sizeof(cur), "UV %d", s.sel.uvChannel);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##uvch", cur)) {
            for (int c = 0; c < sc.uvCount; ++c) {
                char item[16];
                std::snprintf(item, sizeof(item), "UV %d", c);
                if (ImGui::Selectable(item, s.sel.uvChannel == c)) s.sel.uvChannel = c;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::BeginDisabled(s.sel == s.selApplied); // 変更が無ければ押せない
    if (ImGui::Button(Tr(Str::BtnApplySelection))) act.applySelection = true;
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    TextDisabledWrapped(Tr(Str::HintParts));
}

void DrawSettingsPanel(AppState& s, const BakePass& bake, Actions& act) {
    ImGui::SeparatorText(Tr(Str::SecModel));
    if (!s.modelPath.empty()) {
        TextDisabledWrapped(s.modelPath.c_str());
    }
    TextDisabledWrapped(Tr(Str::HintLoadModel));
    ImGui::BeginDisabled(s.loading);
    if (ImGui::Checkbox(Tr(Str::ChkSmoothNormals), &s.smoothNormals)) act.remesh = true;
    ImGui::EndDisabled();
    TextDisabledWrapped(Tr(Str::HintSmoothNormals));
    if (s.loading) {
        const char spin[] = "|/-\\";
        ImGui::Text("%c %s", spin[static_cast<int>(ImGui::GetTime() * 8.0) & 3], Tr(Str::Working));
    }
    ImGui::TextWrapped("%s", FormatStatus(s.modelStatus).c_str());

    DrawPartsPanel(s, act);

    if (s.saveStatus.id != Str::Empty) {
        ImGui::SeparatorText(Tr(Str::SecOutput));
        ImGui::TextWrapped("%s", FormatStatus(s.saveStatus).c_str());
    }

    ImGui::SeparatorText(Tr(Str::SecTexSize));
    ImGui::InputInt(IdLabel(Str::Width, "texW").c_str(), &s.texW, 0, 0);
    ImGui::InputInt(IdLabel(Str::Height, "texH").c_str(), &s.texH, 0, 0);
    s.texW = std::clamp(s.texW, 16, 4096);
    s.texH = std::clamp(s.texH, 16, 4096);
    for (int sz : {256, 512, 1024, 2048, 4096}) {
        char label[16];
        std::snprintf(label, sizeof(label), "%d", sz);
        if (ImGui::SmallButton(label)) s.texW = s.texH = sz;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::Button(Tr(Str::ApplySize))) act.applySize = true;
    ImGui::SameLine();
    ImGui::TextDisabled(Tr(Str::CurrentSize), bake.Width(), bake.Height());

    ImGui::SeparatorText(Tr(Str::SecCurvature));
    if (ImGui::RadioButton(IdLabel(Str::ModeSigned, "modeSigned").c_str(), &s.mode, 0)) s.dirtyShading = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(IdLabel(Str::ModeAbsolute, "modeAbs").c_str(), &s.mode, 1)) s.dirtyShading = true;
    TextDisabledWrapped(Tr(Str::HintMode)); // ラジオボタンのラベルは折り返せないので、説明は別行に出す
    ImGui::TextUnformatted(Tr(Str::Smoothing));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##smooth", &s.smoothing, 0, 64)) s.dirtySmoothing = true;
    TextDisabledWrapped(Tr(Str::HintSmoothing));
    ImGui::TextUnformatted(Tr(Str::Padding));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##padding", &s.padding, 0, 256, "%d", ImGuiSliderFlags_Logarithmic)) s.dirtyPadding = true;
    TextDisabledWrapped(Tr(Str::HintPadding));
    ImGui::TextUnformatted(Tr(Str::MaxCurvature));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##kmax", &s.maxCurv, 1e-3f, 1e4f, "%.4f", ImGuiSliderFlags_Logarithmic)) s.dirtyShading = true;
    if (ImGui::Button(Tr(Str::ResetDefault))) {
        s.maxCurv      = s.defaultMaxCurv;
        s.dirtyShading = true;
    }
    ImGui::TextWrapped("%s", Tr(Str::HintMaxCurvature));
}

// 3Dビュー: 画像を表示し、その上のドラッグ/ホイールでカメラを操作する。
void DrawView3D(AppState& s, const ViewPass& view) {
    if (!s.hasBaked) {
        TextDisabledWrapped(Tr(Str::Empty3D));
        return;
    }
    s.view3dVisible = true;
    ImGui::Checkbox(IdLabel(Str::Lighting, "lit").c_str(), &s.lit);
    ImGui::SameLine();
    if (ImGui::Button(Tr(Str::ResetCamera))) s.camera.Reset();
    ImGui::SameLine();
    TextDisabledWrapped(Tr(Str::CameraHelp));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 16.0f || avail.y < 16.0f) return;
    s.viewReqW = static_cast<int>(avail.x);
    s.viewReqH = static_cast<int>(avail.y);

    // 画像の上に透明なボタンを重ねて、マウス操作を受け取る
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(view.Srv().ptr), avail);
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##view3d", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            s.camera.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            s.camera.Pan(io.MouseDelta.x, io.MouseDelta.y, avail.y);
        }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) s.camera.Zoom(io.MouseWheel);
}

// テクスチャタブ: ホイールでカーソル位置を中心にズーム、ドラッグでパン、カーソル下のピクセル値をツールチップに出す。
// 画像は DrawList の矩形と UV だけで拡大縮小する (GPU 側の処理は変えない)。
void DrawTexture(AppState& s, const BakePass& bake) {
    if (!s.hasBaked) {
        TextDisabledWrapped(Tr(Str::EmptyTexture));
        return;
    }
    const int texW = static_cast<int>(bake.Width());
    const int texH = static_cast<int>(bake.Height());
    TexViewState& v = s.texView;
    if (v.texW != texW || v.texH != texH) { // テクスチャサイズが変わったらフィットに戻す
        v.Reset();
        v.texW = texW;
        v.texH = texH;
    }

    if (ImGui::Button(Tr(Str::ResetView))) v.Reset();
    ImGui::SameLine();
    ImGui::Checkbox(Tr(Str::TexLinear), &v.linear);
    ImGui::SameLine();
    TextDisabledWrapped(Tr(Str::TextureHelp));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 16.0f || avail.y < 16.0f) return;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##texview", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool   hovered = ImGui::IsItemHovered();
    const bool   active  = ImGui::IsItemActive();
    const ImGuiIO& io    = ImGui::GetIO();

    const ImVec2 cmin = origin;
    const ImVec2 cmax(origin.x + avail.x, origin.y + avail.y);
    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);

    const float fit     = std::min(avail.x / texW, avail.y / texH); // フィット時の「1テクセルあたりの画面px」
    const float maxZoom = std::max(1.0f, 128.0f / fit);              // 1テクセルが最大 128px になるまで
    float       scale   = fit * v.zoom;

    // ---- パン ----
    if (active && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
        v.offX += io.MouseDelta.x;
        v.offY += io.MouseDelta.y;
    }

    // ---- ズーム (カーソル下のテクセルが動かないようにオフセットを補正する) ----
    if (hovered && io.MouseWheel != 0.0f) {
        const float tx = (io.MousePos.x - (center.x + v.offX)) / scale; // カーソル下のテクセル座標 (連続値)
        const float ty = (io.MousePos.y - (center.y + v.offY)) / scale;
        v.zoom         = std::clamp(v.zoom * std::pow(1.15f, io.MouseWheel), 0.1f, maxZoom);
        scale          = fit * v.zoom;
        v.offX         = io.MousePos.x - tx * scale - center.x;
        v.offY         = io.MousePos.y - ty * scale - center.y;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        v.Reset();
        scale = fit;
    }

    // ---- 描画: 画面上のテクスチャ全体の矩形 p0-p1 のうち、表示領域に入る部分だけを UV 付きで描く ----
    const ImVec2 p0(center.x + v.offX - texW * scale * 0.5f, center.y + v.offY - texH * scale * 0.5f);
    const ImVec2 p1(p0.x + texW * scale, p0.y + texH * scale);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(cmin, cmax, true);
    const ImVec2 q0(std::max(p0.x, cmin.x), std::max(p0.y, cmin.y));
    const ImVec2 q1(std::min(p1.x, cmax.x), std::min(p1.y, cmax.y));
    if (q0.x < q1.x && q0.y < q1.y) {
        const ImVec2 uv0((q0.x - p0.x) / (p1.x - p0.x), (q0.y - p0.y) / (p1.y - p0.y));
        const ImVec2 uv1((q1.x - p0.x) / (p1.x - p0.x), (q1.y - p0.y) / (p1.y - p0.y));
        // 補間なしのときは、この画像だけ点サンプラー版のパイプラインに切り替えて描く (作れていなければ線形補間のまま)
        const bool point = !v.linear && UiPointSampler::Instance().Ready();
        if (point) dl->AddCallback(UiPointSampler::Begin, nullptr);
        dl->AddImage(static_cast<ImTextureID>(bake.ResultSrv().ptr), q0, q1, uv0, uv1);
        if (point) dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    // 十分に拡大したらテクセルの境界線を重ねる
    if (scale >= 8.0f) {
        const ImU32 gc = IM_COL32(255, 255, 255, 40);
        const int   x0 = std::max(0, static_cast<int>(std::floor((cmin.x - p0.x) / scale)));
        const int   x1 = std::min(texW, static_cast<int>(std::ceil((cmax.x - p0.x) / scale)));
        const int   y0 = std::max(0, static_cast<int>(std::floor((cmin.y - p0.y) / scale)));
        const int   y1 = std::min(texH, static_cast<int>(std::ceil((cmax.y - p0.y) / scale)));
        for (int x = x0; x <= x1; ++x) dl->AddLine(ImVec2(p0.x + x * scale, cmin.y), ImVec2(p0.x + x * scale, cmax.y), gc);
        for (int y = y0; y <= y1; ++y) dl->AddLine(ImVec2(cmin.x, p0.y + y * scale), ImVec2(cmax.x, p0.y + y * scale), gc);
    }

    // ---- ピクセル値 ----
    if (hovered && !active) {
        const int px = static_cast<int>(std::floor((io.MousePos.x - p0.x) / scale));
        const int py = static_cast<int>(std::floor((io.MousePos.y - p0.y) / scale));
        if (px >= 0 && px < texW && py >= 0 && py < texH) {
            s.texPixelsWanted = true; // 読み戻しは App 側 (カーソルがテクスチャ上にある間だけ行う)
            if (scale >= 4.0f) {
                dl->AddRect(ImVec2(p0.x + px * scale, p0.y + py * scale), ImVec2(p0.x + (px + 1) * scale, p0.y + (py + 1) * scale), IM_COL32(255, 200, 0, 255));
            }

            const TexPixels& tp = s.texPixels;
            const bool       ok = tp.version == bake.Version() && tp.width == texW && tp.height == texH &&
                            tp.rgba.size() >= static_cast<size_t>(texW) * texH * 4;
            ImGui::BeginTooltip();
            ImGui::Text("%s: %d, %d", Tr(Str::LabelPixel), px, py);
            ImGui::Text("UV: %.4f, %.4f", (px + 0.5f) / texW, (py + 0.5f) / texH);
            if (ok) {
                const uint8_t* c = &tp.rgba[(static_cast<size_t>(py) * texW + px) * 4];
                const float    n = c[0] / 255.0f; // 結果テクスチャはグレー (R=G=B)
                // シェーダー (shade.hlsl) の Display() の逆変換。0 / 1 は飽和しているので範囲外を示す。
                const float k = (tp.mode == 0) ? (n * 2.0f - 1.0f) * tp.maxCurv : n * tp.maxCurv;
                const char* sat = "";
                if (tp.mode == 0) sat = (c[0] == 255) ? ">= " : (c[0] == 0) ? "<= " : "";
                else sat = (c[0] == 255) ? ">= " : "";
                ImGui::Text("%s: %u (%.4f)", Tr(Str::LabelValue), static_cast<unsigned>(c[0]), n);
                ImGui::Text("%s: %s%.4f", Tr(Str::LabelCurvature), sat, k);
            }
            ImGui::EndTooltip();
        }
    }
    dl->PopClipRect();
}

} // namespace

void DrawUI(AppState& s, const BakePass& bake, const ViewPass& view, Actions& act) {
    // メニューバーを先に出す。メインウィンドウは、その分を除いた作業領域 (WorkPos / WorkSize) に置く。
    DrawMenuBar(s, view, act);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Main", nullptr, flags);

    const float leftW = 380.0f * ImGui::GetStyle().FontScaleDpi;

    // ---- 左: 設定パネル ----
    ImGui::BeginChild("Settings", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
    DrawSettingsPanel(s, bake, act);
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 右: プレビュー (タブで 3Dビュー / テクスチャ を切り替え) ----
    ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    s.view3dVisible = false;
    s.texPixelsWanted = false;
    if (ImGui::BeginTabBar("PreviewTabs")) {
        if (ImGui::BeginTabItem(IdLabel(Str::TabView3D, "tabView3D").c_str())) {
            DrawView3D(s, view);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IdLabel(Str::TabTexture, "tabTexture").c_str())) {
            DrawTexture(s, bake);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();
}
