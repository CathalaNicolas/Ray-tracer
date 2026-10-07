cmake_minimum_required(VERSION 3.24)
project(raytracer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RAYTRACER_DILIGENT "Build Diligent clustered-forward path" ON)
option(RAYTRACER_BUILD_EDITOR "Build the interactive editor/game exe" ON)

include(FetchContent)

# --- Diligent (Core / Tools / FX) ---------------------------------------------
# DiligentTools/FX headers use sibling relative includes (../../../DiligentCore).
# FetchContent places each repo under _deps/<name>-src, so we symlink the
# expected DiligentCore / DiligentTools names beside them before configuring Tools/FX.
if(RAYTRACER_DILIGENT)
  if(WIN32)
    set(DILIGENT_NO_VULKAN ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_DIRECT3D12 OFF CACHE BOOL "" FORCE)
    set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
  else()
    set(DILIGENT_NO_VULKAN OFF CACHE BOOL "" FORCE)
    set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
  endif()
  set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
  set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(DILIGENT_BUILD_FX ON CACHE BOOL "" FORCE)
  set(DILIGENT_BUILD_TOOLS ON CACHE BOOL "" FORCE)
  set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DILIGENT_NO_FORMAT_VALIDATION ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_RENDER_STATE_PACKAGER ON CACHE BOOL "" FORCE)
  set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "" FORCE)
  set(DILIGENT_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
  set(DILIGENT_INSTALL_FX OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    DiligentCore
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
    GIT_TAG v2.5.6
    GIT_SHALLOW TRUE
  )
  FetchContent_Declare(
    DiligentTools
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentTools.git
    GIT_TAG v2.5.6
    GIT_SHALLOW TRUE
  )
  FetchContent_Declare(
    DiligentFX
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentFX.git
    GIT_TAG v2.5.6
    GIT_SHALLOW TRUE
  )

  FetchContent_GetProperties(DiligentCore)
  if(NOT diligentcore_POPULATED)
    FetchContent_Populate(DiligentCore)
    # Sibling name expected by Tools/FX relative #includes.
    file(CREATE_LINK "${diligentcore_SOURCE_DIR}"
      "${CMAKE_BINARY_DIR}/_deps/DiligentCore" SYMBOLIC)
    add_subdirectory(${diligentcore_SOURCE_DIR} ${diligentcore_BINARY_DIR})
  endif()

  FetchContent_GetProperties(DiligentTools)
  if(NOT diligenttools_POPULATED)
    FetchContent_Populate(DiligentTools)
    file(CREATE_LINK "${diligenttools_SOURCE_DIR}"
      "${CMAKE_BINARY_DIR}/_deps/DiligentTools" SYMBOLIC)
    add_subdirectory(${diligenttools_SOURCE_DIR} ${diligenttools_BINARY_DIR})
  endif()

  FetchContent_GetProperties(DiligentFX)
  if(NOT diligentfx_POPULATED)
    FetchContent_Populate(DiligentFX)
    add_subdirectory(${diligentfx_SOURCE_DIR} ${diligentfx_BINARY_DIR})
  endif()
endif()

# --- SDL3 (window / input; Diligent takes the native handle) ------------------
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.18
  GIT_SHALLOW TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
set(SDL_JACK OFF CACHE BOOL "" FORCE)
set(SDL_PULSEAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_ALSA OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# --- Core library (no window / no Diligent) ----------------------------------
set(RAYTRACER_CORE_SOURCES
  engine/src/Collision.cpp
  engine/src/EngineSettings.cpp
  engine/src/Mesh.cpp
  engine/src/Plane.cpp
  engine/src/Play.cpp
  engine/src/Prefab.cpp
  engine/src/Role.cpp
  engine/src/SceneFile.cpp
  engine/src/SceneLoad.cpp
  engine/src/SceneWrite.cpp
  engine/src/Sphere.cpp
  engine/src/image_read.cpp
  engine/src/image_write.cpp
)

add_library(raytracer_core STATIC ${RAYTRACER_CORE_SOURCES})
target_include_directories(raytracer_core PUBLIC
  "${CMAKE_SOURCE_DIR}/engine/include"
  "${CMAKE_SOURCE_DIR}/third_party"
)
target_compile_definitions(raytracer_core PUBLIC NOMINMAX)
if(MSVC)
  target_compile_options(raytracer_core PRIVATE /W4 /EHsc /bigobj)
endif()

# --- Legacy OpenGL still / self-test path (Windows) --------------------------
if(WIN32)
  set(RAYTRACER_GL_SOURCES
    engine/src/GpuGl.cpp
    engine/src/GpuShaders.cpp
    engine/src/GpuShaderTrace.cpp
    engine/src/GpuRaster.cpp
    engine/src/GpuRayTracer.cpp
    engine/src/GpuRender.cpp
    engine/src/RayTracer.cpp
    engine/src/self_test.cpp
    engine/src/PlayTests.cpp
    engine/src/SceneDebug.cpp
    engine/src/Sound.cpp
  )
  add_library(raytracer_gl STATIC ${RAYTRACER_GL_SOURCES})
  target_link_libraries(raytracer_gl PUBLIC raytracer_core opengl32)
  target_include_directories(raytracer_gl PRIVATE "${CMAKE_SOURCE_DIR}/engine/src")
endif()

# --- Diligent RHI (clustered forward primary view) ---------------------------
if(RAYTRACER_DILIGENT)
  set(RAYTRACER_RHI_SOURCES
    engine/src/GfxDevice.cpp
    engine/src/GfxView.cpp
    engine/src/GfxCluster.cpp
    engine/src/GfxQuality.cpp
    engine/src/GfxTextures.cpp
    engine/src/GfxFx.cpp
  )
  add_library(raytracer_rhi STATIC ${RAYTRACER_RHI_SOURCES})
  target_link_libraries(raytracer_rhi PUBLIC raytracer_core Diligent-GraphicsEngine)
  if(WIN32)
    target_link_libraries(raytracer_rhi PUBLIC Diligent-GraphicsEngineD3D12-static)
    target_compile_definitions(raytracer_rhi PUBLIC RAYTRACER_DILIGENT_D3D12=1)
  else()
    target_link_libraries(raytracer_rhi PUBLIC Diligent-GraphicsEngineVk-static)
    target_compile_definitions(raytracer_rhi PUBLIC RAYTRACER_DILIGENT_VULKAN=1)
  endif()
  target_link_libraries(raytracer_rhi PUBLIC
    Diligent-TextureLoader
    Diligent-Imgui
    DiligentFX
    Diligent-GraphicsTools
    SDL3::SDL3-static
  )
  target_include_directories(raytracer_rhi PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/include"
    "${CMAKE_SOURCE_DIR}/engine/src"
  )
  target_compile_definitions(raytracer_rhi PUBLIC RAYTRACER_DILIGENT=1)
  # Shader directory baked for runtime load.
  target_compile_definitions(raytracer_rhi PRIVATE
    "RAYTRACER_SHADER_DIR=\"${CMAKE_SOURCE_DIR}/engine/shaders\""
  )

  add_executable(diligent_smoke tools/diligent_smoke.cpp)
  target_link_libraries(diligent_smoke PRIVATE raytracer_rhi)
  if(WIN32)
    target_link_libraries(diligent_smoke PRIVATE user32 gdi32)
  endif()
endif()

# --- Editor / game exe -------------------------------------------------------
if(RAYTRACER_BUILD_EDITOR AND RAYTRACER_DILIGENT)
  # Primary interactive path: SDL + Diligent (D3D12 on Windows, Vulkan elsewhere).
  # Legacy Win32+GL editor sources are not linked; GpuRayTracer remains for --self-test on Windows.
  set(RAYTRACER_EDITOR_SOURCES
    editor/src/main.cpp
    editor/src/EditorDiligent.cpp
  )
  if(WIN32 AND TARGET raytracer_gl)
    # Still / --self-test keep the WGL host via raytracer_gl.
  else()
    list(APPEND RAYTRACER_EDITOR_SOURCES
      engine/src/RayTracer.cpp
      engine/src/SelfTestStub.cpp
    )
  endif()
  add_executable(raytracer ${RAYTRACER_EDITOR_SOURCES})
  target_include_directories(raytracer PRIVATE editor/include)
  target_link_libraries(raytracer PRIVATE raytracer_core raytracer_rhi)
  if(WIN32)
    target_link_libraries(raytracer PRIVATE user32 gdi32)
    if(TARGET raytracer_gl)
      target_link_libraries(raytracer PRIVATE raytracer_gl)
    endif()
  endif()
  if(MSVC)
    target_compile_options(raytracer PRIVATE /W4 /EHsc /bigobj)
  endif()
elseif(RAYTRACER_BUILD_EDITOR AND WIN32)
  set(RAYTRACER_IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
  )
  add_executable(raytracer
    editor/src/main.cpp
    editor/src/editor.cpp
    editor/src/EditorPlay.cpp
    editor/src/EditorScene.cpp
    editor/src/EditorHistory.cpp
    editor/src/EditorUi.cpp
    editor/src/EditorWidgets.cpp
    editor/src/DebugDraw.cpp
    ${RAYTRACER_IMGUI_SOURCES}
  )
  target_include_directories(raytracer PRIVATE
    editor/include
    third_party/imgui
    third_party/imgui/backends
  )
  target_link_libraries(raytracer PRIVATE
    raytracer_core
    raytracer_gl
    user32 gdi32 opengl32 comdlg32 advapi32 winmm
  )
  if(MSVC)
    target_compile_options(raytracer PRIVATE /W4 /EHsc /bigobj)
    set_source_files_properties(${RAYTRACER_IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
  endif()
endif()

# --- Headless self-test (core + GL still path on Windows) --------------------
if(WIN32)
  add_executable(tests tools/headless_tests.cpp)
  target_link_libraries(tests PRIVATE raytracer_core raytracer_gl)
endif()
