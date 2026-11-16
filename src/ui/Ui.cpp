#include "ui/Ui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "i18n/Strings.h"
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
                const Lang cur = GetLang();
                // 言語名は、その言語自身の表記で出す (どの言語で動いていても選べるように)
                for (int i = 0; i < kLangCount; ++i) {
                    const Lang l = static_cast<Lang>(i);
                    if (ImGui::MenuItem(LangNativeName(l), nullptr, cur == l)) SetLang(l);
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

void DrawTexture(const AppState& s, const BakePass& bake) {
    if (!s.hasBaked) {
        TextDisabledWrapped(Tr(Str::EmptyTexture));
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  w     = static_cast<float>(bake.Width());
    const float  h     = static_cast<float>(bake.Height());
    const float  scale = std::min(avail.x / w, avail.y / h);
    ImGui::Image(static_cast<ImTextureID>(bake.ResultSrv().ptr), ImVec2(w * scale, h * scale));
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
