cmake_minimum_required(VERSION 3.24)
project(curvtex LANGUAGES CXX C RC)  # C: ufbx.c のコンパイル用 / RC: アイコン等のリソース

if(NOT WIN32)
  message(FATAL_ERROR "This project requires Windows (DirectX 12).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ---- Dear ImGui v1.93.0 -----------------------------------------------------
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.93.0
  GIT_SHALLOW    TRUE
)

# ---- rapidobj (header only) -------------------------------------------------
# SOURCE_SUBDIR に存在しないディレクトリを指定し、rapidobj側のCMakeLists.txtは使わない
FetchContent_Declare(
  rapidobj
  GIT_REPOSITORY https://github.com/guybrush77/rapidobj.git
  GIT_TAG        v1.1
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_here
)

# ---- fastgltf v1.0.1 (.gltf / .glb) -----------------------------------------
# simdjson / simdutf は fastgltf が FetchContent で取得する。テスト・サンプル・ドキュメントは不要。
set(FASTGLTF_DOWNLOAD_SIMDJSON ON  CACHE BOOL "" FORCE)
set(FASTGLTF_DOWNLOAD_SIMDUTF  ON  CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS      OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG        v1.0.1
  GIT_SHALLOW    TRUE
)

# ---- ufbx v0.23.1 (.fbx, single source file) --------------------------------
# ufbx 側の CMake は使わず、ufbx.c を自前でライブラリ化する
FetchContent_Declare(
  ufbx
  GIT_REPOSITORY https://github.com/ufbx/ufbx.git
  GIT_TAG        v0.23.1
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _no_cmake_here
)

FetchContent_MakeAvailable(imgui rapidobj fastgltf ufbx)

add_library(ufbx_lib STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx_lib PUBLIC ${ufbx_SOURCE_DIR})

# ---- ImGui static library ---------------------------------------------------
add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC d3d12 dxgi d3dcompiler dwmapi)

# ---- シェーダの事前コンパイル (fxc) -----------------------------------------
# shaders/*.hlsl をビルド時にコンパイルし、バイトコードを C 配列ヘッダとして生成する。
if(NOT FXC_EXECUTABLE)
  # 括弧は CMake の変数名に使えないのでエスケープする
  set(_pf86 "$ENV{ProgramFiles\(x86\)}")
  if(NOT _pf86)
    set(_pf86 "C:/Program Files (x86)")
  endif()
  file(GLOB _sdk_bin_dirs "${_pf86}/Windows Kits/10/bin/*/x64")
  list(SORT _sdk_bin_dirs COMPARE NATURAL ORDER DESCENDING)
  find_program(FXC_EXECUTABLE fxc HINTS ${_sdk_bin_dirs})
endif()
if(NOT FXC_EXECUTABLE)
  message(FATAL_ERROR "fxc.exe が見つかりません。Windows SDK をインストールするか -DFXC_EXECUTABLE=<path> を指定してください。")
endif()

set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

# Debug: 最適化なし+デバッグ情報 / それ以外: 最適化あり
set(FXC_FLAGS /nologo /Ges
  $<$<CONFIG:Debug>:/Zi>
  $<$<CONFIG:Debug>:/Od>
  $<$<NOT:$<CONFIG:Debug>>:/O3>)

set(SHADER_HEADERS)
function(add_shader name hlsl entry profile varname)
  set(out ${SHADER_OUT_DIR}/${name}.h)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND ${FXC_EXECUTABLE} ${FXC_FLAGS} /E ${entry} /T ${profile} /Vn ${varname} /Fh ${out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${hlsl}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${hlsl}
    COMMENT "fxc ${hlsl} (${entry}, ${profile})"
    COMMAND_EXPAND_LISTS # 空に評価された生成式が "" 引数として渡るのを防ぐ
    VERBATIM)
  set(SHADER_HEADERS ${SHADER_HEADERS} ${out} PARENT_SCOPE)
endfunction()

add_shader(raster_vs   raster.hlsl    VSMain       vs_5_0 g_RasterVS)
add_shader(raster_ps   raster.hlsl    PSMain       ps_5_0 g_RasterPS)
add_shader(curvature_cs curvature.hlsl CSCurvature  cs_5_0 g_CurvatureCS)
add_shader(smooth_cs   curvature.hlsl CSSmooth     cs_5_0 g_SmoothCS)
add_shader(shade_vs    shade.hlsl     VSMain       vs_5_0 g_ShadeVS)
add_shader(shade_ps    shade.hlsl     PSMain       ps_5_0 g_ShadePS)
add_shader(jfa_vs      jfa.hlsl  VSMain  vs_5_0 g_JfaVS)
add_shader(jfa_init_ps jfa.hlsl  PSInit  ps_5_0 g_JfaInitPS)
add_shader(jfa_step_ps jfa.hlsl  PSStep  ps_5_0 g_JfaStepPS)
add_shader(view_vs  view.hlsl  VSMain vs_5_0 g_ViewVS)
add_shader(view_ps  view.hlsl  PSMain ps_5_0 g_ViewPS)
add_shader(mip_vs   mip.hlsl   VSMain vs_5_0 g_MipVS)
add_shader(mip_ps   mip.hlsl   PSMain ps_5_0 g_MipPS)
add_shader(ui_vs    ui_point.hlsl VSMain vs_5_0 g_UiVS)
add_shader(ui_ps    ui_point.hlsl PSMain ps_5_0 g_UiPS)

# ---- Application ------------------------------------------------------------
find_package(Threads REQUIRED) # rapidobj は std::thread を使用

add_executable(curvtex WIN32
  # アプリ
  src/app/main.cpp
  src/app/App.cpp
  src/app/App.h
  src/app/AppState.h
  # 多言語化 (UI文字列・エラー文言の日本語/英語)
  src/i18n/Strings.cpp
  src/i18n/Strings.h
  # UI
  src/ui/Ui.cpp
  src/ui/Ui.h
  src/ui/Camera.cpp
  src/ui/Camera.h
  # プラットフォーム(Win32)
  src/platform/Window.cpp
  src/platform/Window.h
  src/platform/Dialogs.cpp
  src/platform/Dialogs.h
  src/platform/PathUtil.h
  src/platform/resource.h
  src/platform/curvtex.rc
  res/icon.ico
  src/platform/curvtex.manifest # Per-Monitor DPI v2 (MSVC では自動で実行ファイルに埋め込まれる)
  # DirectX12
  src/gpu/Renderer.cpp
  src/gpu/Renderer.h
  src/gpu/GpuUtil.cpp
  src/gpu/GpuUtil.h
  src/gpu/CurvatureCompute.cpp
  src/gpu/CurvatureCompute.h
  src/gpu/GpuMesh.cpp
  src/gpu/GpuMesh.h
  src/gpu/BakePass.cpp
  src/gpu/BakePass.h
  src/gpu/ViewPass.cpp
  src/gpu/ViewPass.h
  src/gpu/UiPointSampler.cpp
  src/gpu/UiPointSampler.h
  # モデル読み込み
  src/mesh/Mesh.cpp
  src/mesh/Mesh.h
  src/mesh/Weld.cpp
  src/mesh/Weld.h
  src/mesh/CurvatureGraph.cpp
  src/mesh/CurvatureGraph.h
  src/mesh/MeshBuild.cpp
  src/mesh/MeshBuild.h
  src/mesh/MeshUtil.h
  src/mesh/MeshGltf.cpp
  src/mesh/MeshFbx.cpp
  # 画像書き出し
  src/image/ImageWriter.cpp
  src/image/ImageWriter.h
  src/image/Bmp.cpp
  src/image/Bmp.h
  # シェーダ
  shaders/curvature.hlsl
  shaders/raster.hlsl
  shaders/shade.hlsl
  shaders/jfa.hlsl
  shaders/view.hlsl
  shaders/mip.hlsl
  shaders/ui_point.hlsl
  ${SHADER_HEADERS}
)

# .hlsl は VS では自動コンパイルさせない (上の fxc ルールで処理する)
set_source_files_properties(shaders/curvature.hlsl shaders/raster.hlsl shaders/shade.hlsl shaders/jfa.hlsl shaders/view.hlsl shaders/mip.hlsl shaders/ui_point.hlsl PROPERTIES VS_TOOL_OVERRIDE "None")

target_include_directories(curvtex PRIVATE
  ${rapidobj_SOURCE_DIR}/include
  src
  ${SHADER_OUT_DIR}
)
target_link_libraries(curvtex PRIVATE imgui_lib fastgltf::fastgltf ufbx_lib Threads::Threads comdlg32 windowscodecs ole32)
target_compile_definitions(curvtex PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  # ソースはUTF-8（日本語UI文字列を含む）
  target_compile_options(curvtex PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT curvtex)
endif()
