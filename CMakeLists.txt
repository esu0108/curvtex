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
# d3dcompiler は ImGui の DX12 バックエンドが、自前のシェーダーを実行時にコンパイルするのに使う (このアプリのシェーダーは dxc で事前コンパイル済み)
target_link_libraries(imgui_lib PUBLIC d3d12 dxgi d3dcompiler dwmapi)

# ---- シェーダの事前コンパイル (DirectX Shader Compiler: dxc) -----------------
# shaders/*.hlsl をビルド時に dxc でコンパイルし、DXIL (シェーダーモデル 6.0) の .cso を作る。
# 探す場所: -DDXC_EXECUTABLE=<path> → Windows SDK (bin/<version>/x64) → Vulkan SDK → PATH。
# 配布元: https://github.com/microsoft/DirectXShaderCompiler/releases (zip の bin/x64 の dxc.exe)
if(NOT DXC_EXECUTABLE)
  # 括弧は CMake の変数名に使えないのでエスケープする
  set(_pf86 "$ENV{ProgramFiles\(x86\)}")
  if(NOT _pf86)
    set(_pf86 "C:/Program Files (x86)")
  endif()
  file(GLOB _sdk_bin_dirs "${_pf86}/Windows Kits/10/bin/*/x64")
  list(SORT _sdk_bin_dirs COMPARE NATURAL ORDER DESCENDING)
  find_program(DXC_EXECUTABLE dxc HINTS ${_sdk_bin_dirs} "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
endif()
if(NOT DXC_EXECUTABLE)
  message(FATAL_ERROR "dxc.exe (DirectX Shader Compiler) が見つかりません。最近の Windows SDK をインストールするか、"
                      "https://github.com/microsoft/DirectXShaderCompiler/releases の zip を展開して -DDXC_EXECUTABLE=<bin/x64/dxc.exe のパス> を指定してください。")
endif()
# dxc は、隣の dxil.dll で DXIL に署名する。署名の無いシェーダーは D3D12 がパイプライン作成で拒否する。
get_filename_component(_dxc_dir "${DXC_EXECUTABLE}" DIRECTORY)
if(NOT EXISTS "${_dxc_dir}/dxil.dll")
  message(WARNING "${_dxc_dir} に dxil.dll がありません。署名の無いシェーダーは実行時に拒否されます。dxc.exe と dxil.dll が同じフォルダにあるものを使ってください。")
endif()

# コンパイル済みシェーダー (.cso) は、ビルド設定ごとのフォルダへ作る (Debug と Release で最適化が違うので混ぜない)。
# できた .cso は、ビルド後に実行ファイルの隣の assets/shaders へコピーされ、実行時にそこから読み込む。
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/$<CONFIG>/shaders)

# Debug: 最適化なし+デバッグ情報 (.cso に埋め込み) / それ以外: 最適化あり
set(DXC_FLAGS
  $<$<CONFIG:Debug>:-Zi>
  $<$<CONFIG:Debug>:-Qembed_debug>
  $<$<CONFIG:Debug>:-Od>
  $<$<NOT:$<CONFIG:Debug>>:-O3>)

set(SHADER_BLOBS)
function(add_shader name hlsl entry stage)
  set(out ${SHADER_OUT_DIR}/${name}.cso)
  add_custom_command(
    OUTPUT  ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
    COMMAND ${DXC_EXECUTABLE} ${DXC_FLAGS} -E ${entry} -T ${stage}_${CURVTEX_SHADER_MODEL} -Fo ${out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${hlsl}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${hlsl}
    COMMENT "dxc ${hlsl} (${entry}, ${stage}_${CURVTEX_SHADER_MODEL}) -> ${name}.cso"
    COMMAND_EXPAND_LISTS # 空に評価された生成式が "" 引数として渡るのを防ぐ
    VERBATIM)
  set(SHADER_BLOBS ${SHADER_BLOBS} ${out} PARENT_SCOPE)
endfunction()

# シェーダーモデル。DXIL の最低は 6.0 (実行時に GPU・ドライバーが対応しているか確認する: gpu/Renderer.cpp)
set(CURVTEX_SHADER_MODEL 6_0)

# 名前 (.cso のファイル名。コードの LoadShader("...") と同じ) / HLSL / エントリ / ステージ (vs, ps, cs)
add_shader(raster_vs    raster.hlsl    VSMain      vs)
add_shader(raster_ps    raster.hlsl    PSMain      ps)
add_shader(curvature_cs curvature.hlsl CSCurvature cs)
add_shader(smooth_cs    curvature.hlsl CSSmooth    cs)
add_shader(shade_vs     shade.hlsl     VSMain      vs)
add_shader(shade_ps     shade.hlsl     PSMain      ps)
add_shader(jfa_vs       jfa.hlsl       VSMain      vs)
add_shader(jfa_init_ps  jfa.hlsl       PSInit      ps)
add_shader(jfa_step_ps  jfa.hlsl       PSStep      ps)
add_shader(view_vs      view.hlsl      VSMain      vs)
add_shader(view_ps      view.hlsl      PSMain      ps)
add_shader(mip_vs       mip.hlsl       VSMain      vs)
add_shader(mip_ps       mip.hlsl       PSMain      ps)
add_shader(ui_vs        ui_point.hlsl  VSMain      vs)
add_shader(ui_ps        ui_point.hlsl  PSMain      ps)

add_custom_target(curvtex_shaders DEPENDS ${SHADER_BLOBS})

# ---- Application ------------------------------------------------------------
find_package(Threads REQUIRED) # rapidobj は std::thread を使用

add_executable(curvtex WIN32
  # アプリ
  src/app/main.cpp
  src/app/App.cpp
  src/app/App.h
  src/app/AppState.h
  # 多言語化 (英語を内蔵し、各言語は lang/*.lang から読み込む)
  src/i18n/Strings.cpp
  src/i18n/Strings.h
  src/i18n/LangFile.cpp
  src/i18n/LangFile.h
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
  src/platform/AssetPath.h
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
)
add_dependencies(curvtex curvtex_shaders) # シェーダーを先にコンパイルしておく

# .hlsl は VS では自動コンパイルさせない (上の dxc ルールで処理する)
set_source_files_properties(shaders/curvature.hlsl shaders/raster.hlsl shaders/shade.hlsl shaders/jfa.hlsl shaders/view.hlsl shaders/mip.hlsl shaders/ui_point.hlsl PROPERTIES VS_TOOL_OVERRIDE "None")

target_include_directories(curvtex PRIVATE
  ${rapidobj_SOURCE_DIR}/include
  src
)
# 実行時に読み込むファイルを、実行ファイルの隣の assets フォルダへ集める:
#   assets/lang/*.lang      (ソースの assets/lang)
#   assets/shaders/*.cso    (上でコンパイルしたシェーダー)
add_custom_command(TARGET curvtex POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:curvtex>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_OUT_DIR} $<TARGET_FILE_DIR:curvtex>/assets/shaders
  VERBATIM)

target_link_libraries(curvtex PRIVATE imgui_lib fastgltf::fastgltf ufbx_lib Threads::Threads comdlg32 windowscodecs ole32)
target_compile_definitions(curvtex PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  # ソースはUTF-8（日本語UI文字列を含む）
  target_compile_options(curvtex PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT curvtex)
endif()
