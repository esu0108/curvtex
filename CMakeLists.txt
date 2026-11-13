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
)

target_include_directories(curvtex PRIVATE
  ${rapidobj_SOURCE_DIR}/include
  src
)
target_link_libraries(curvtex PRIVATE imgui_lib Threads::Threads comdlg32)
target_compile_definitions(curvtex PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  # ソースはUTF-8（日本語UI文字列を含む）
  target_compile_options(curvtex PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT curvtex)
endif()
