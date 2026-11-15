#pragma once
#include "app/AppState.h"
#include "gpu/BakePass.h"
#include "gpu/ViewPass.h"

// 1フレーム分の ImGui UI を組み立てる。状態は s に反映し、アプリへの要求は act に積む。
void DrawUI(AppState& s, const BakePass& bake, const ViewPass& view, Actions& act);
