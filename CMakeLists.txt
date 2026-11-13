cmake_minimum_required(VERSION 3.24)
project(curvtex LANGUAGES CXX)

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

FetchContent_MakeAvailable(imgui rapidobj)

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

add_shader(pass1_vs pass1.hlsl VSMain vs_5_0 g_Pass1VS)
add_shader(pass1_ps pass1.hlsl PSMain ps_5_0 g_Pass1PS)
add_shader(pass2_vs pass2.hlsl VSMain vs_5_0 g_Pass2VS)
add_shader(pass2_ps pass2.hlsl PSMain ps_5_0 g_Pass2PS)

# ---- Application ------------------------------------------------------------
find_package(Threads REQUIRED) # rapidobj は std::thread を使用

add_executable(curvtex WIN32
  src/main.cpp
  src/Renderer.cpp
  src/Renderer.h
  src/Mesh.cpp
  src/Mesh.h
  src/Bmp.cpp
  src/Bmp.h
  shaders/pass1.hlsl
  shaders/pass2.hlsl
  ${SHADER_HEADERS}
)

# .hlsl は VS では自動コンパイルさせない (上の fxc ルールで処理する)
set_source_files_properties(shaders/pass1.hlsl shaders/pass2.hlsl PROPERTIES VS_TOOL_OVERRIDE "None")

target_include_directories(curvtex PRIVATE
  ${rapidobj_SOURCE_DIR}/include
  src
  ${SHADER_OUT_DIR}
)
target_link_libraries(curvtex PRIVATE imgui_lib Threads::Threads comdlg32)
target_compile_definitions(curvtex PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  # ソースはUTF-8（日本語UI文字列を含む）
  target_compile_options(curvtex PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT curvtex)
endif()
