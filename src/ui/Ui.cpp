#include "ui/Ui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace {

// std::string 用の InputText (imgui_stdlib と同じ仕組み)。長さの上限が無い。
int ResizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

bool InputTextStd(const char* label, std::string* str) {
    return ImGui::InputText(label, str->data(), str->capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeCallback, str);
}

void DrawSettingsPanel(AppState& s, const BakePass& bake, Actions& act) {
    ImGui::SeparatorText("モデル");
    ImGui::BeginDisabled(s.loading);
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextStd("##model", &s.modelPath);
    if (ImGui::Button("参照...")) act.browseModel = true;
    ImGui::SameLine();
    if (ImGui::Button("読み込み")) act.load = true;
    ImGui::TextDisabled("ウィンドウへのドラッグ&ドロップでも読み込めます");
    if (ImGui::Checkbox("スムース法線を再計算", &s.smoothNormals)) act.remesh = true;
    ImGui::EndDisabled();
    ImGui::TextDisabled("位置が同じ頂点を溶接して法線を平滑化します");
    if (s.loading) {
        const char spin[] = "|/-\\";
        ImGui::Text("%s 処理中...", std::string(1, spin[static_cast<int>(ImGui::GetTime() * 8.0) & 3]).c_str());
    }
    ImGui::TextWrapped("%s", s.modelStatus.c_str());

    ImGui::SeparatorText("テクスチャサイズ");
    ImGui::InputInt("幅", &s.texW, 0, 0);
    ImGui::InputInt("高さ", &s.texH, 0, 0);
    s.texW = std::clamp(s.texW, 16, 4096);
    s.texH = std::clamp(s.texH, 16, 4096);
    for (int sz : {256, 512, 1024, 2048, 4096}) {
        char label[16];
        std::snprintf(label, sizeof(label), "%d", sz);
        if (ImGui::SmallButton(label)) s.texW = s.texH = sz;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::Button("サイズを適用")) act.applySize = true;
    ImGui::SameLine();
    ImGui::TextDisabled("現在: %u x %u", bake.Width(), bake.Height());

    ImGui::SeparatorText("曲率");
    if (ImGui::RadioButton("符号付き (0.5=平坦, 凸>0.5, 凹<0.5)", &s.mode, 0)) s.dirtyShading = true;
    if (ImGui::RadioButton("絶対値 (0=平坦)", &s.mode, 1)) s.dirtyShading = true;
    ImGui::Text("スムージング (回)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##smooth", &s.smoothing, 0, 64)) s.dirtySmoothing = true;
    ImGui::TextDisabled("頂点ごとの曲率を隣接頂点と平均化する回数。大きいほど滑らか(0でそのまま)");
    ImGui::Text("パディング (テクセル)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##padding", &s.padding, 0, 256, "%d", ImGuiSliderFlags_Logarithmic)) s.dirtyPadding = true;
    ImGui::TextDisabled("UV島の外側を最寄りの値で塗り広げ、継ぎ目の黒い線を防ぎます");
    ImGui::Text("最大曲率 (1/単位長)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##kmax", &s.maxCurv, 1e-3f, 1e4f, "%.4f", ImGuiSliderFlags_Logarithmic)) s.dirtyShading = true;
    if (ImGui::Button("初期値に戻す")) {
        s.maxCurv      = s.defaultMaxCurv;
        s.dirtyShading = true;
    }
    ImGui::TextWrapped("この曲率で出力が0/1に飽和します。半径Rの球の曲率は 1/R です。");

    ImGui::SeparatorText("画像保存 (PNG / BMP / TGA)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextStd("##save", &s.savePath);
    if (ImGui::Button("参照...##save")) act.browseSave = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.hasBaked || s.saving); // 保存中は押せない
    if (ImGui::Button("保存")) act.save = true;
    ImGui::EndDisabled();
    if (!s.saveStatus.empty()) ImGui::TextWrapped("%s", s.saveStatus.c_str());
}

// 3Dビュー: 画像を表示し、その上のドラッグ/ホイールでカメラを操作する。
void DrawView3D(AppState& s, const ViewPass& view) {
    if (!s.hasBaked) {
        ImGui::TextDisabled("モデルを読み込むと、ここに曲率テクスチャを貼った3Dモデルが表示されます。");
        return;
    }
    s.view3dVisible = true;
    ImGui::Checkbox("ライティング", &s.lit);
    ImGui::SameLine();
    if (ImGui::Button("カメラをリセット")) s.camera.Reset();
    ImGui::SameLine();
    ImGui::TextDisabled("左ドラッグ: 回転 / 右・中ドラッグ: 移動 / ホイール: ズーム");

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
        ImGui::TextDisabled("モデルを読み込むと、ここに曲率テクスチャが表示されます。");
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
        if (ImGui::BeginTabItem("3Dビュー")) {
            DrawView3D(s, view);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("テクスチャ")) {
            DrawTexture(s, bake);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();
}
