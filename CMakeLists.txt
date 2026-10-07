cmake_minimum_required(VERSION 3.25)
project(raytracer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Match the Makefile: Autodesk FBX SDK 2020.3.11 under Program Files (VS2022 install).
set(FBX_SDK_ROOT "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.11" CACHE PATH
  "Autodesk FBX SDK root (same external path the Makefile uses)")

option(RAYTRACER_SHIPPING "Refuse source mesh formats; require cooked .rtm" OFF)
option(RAYTRACER_DILIGENT "Build Diligent clustered-forward primary view" OFF)

if(NOT EXISTS "${FBX_SDK_ROOT}/include/fbxsdk.h")
  message(FATAL_ERROR
    "FBX SDK not found at FBX_SDK_ROOT=${FBX_SDK_ROOT}. "
    "Install FBX SDK 2020.3.11 (VS2022) or pass -DFBX_SDK_ROOT=...")
endif()

find_package(enkiTS CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Bitsery CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(PhysFS CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)

# miniaudio's vcpkg port is header-only (no CONFIG package).
find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
add_library(miniaudio INTERFACE)
add_library(miniaudio::miniaudio ALIAS miniaudio)
target_include_directories(miniaudio INTERFACE "${MINIAUDIO_INCLUDE_DIR}")

# --- Diligent (Core / Tools / FX) ---------------------------------------------
# DiligentTools/FX headers use sibling relative includes (../../../DiligentCore).
# FetchContent places each repo under _deps/<name>-src, so we symlink the
# expected DiligentCore / DiligentTools names beside them before configuring Tools/FX.
if(RAYTRACER_DILIGENT)
  include(FetchContent)
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

set(RAYTRACER_CORE_SOURCES
  engine/src/Jobs.cpp
  engine/src/Log.cpp
  engine/src/Object.cpp
  engine/src/Play.cpp
  engine/src/SimChannel.cpp
  engine/src/SimSerialize.cpp
  engine/src/SimReplay.cpp
  engine/src/SimSave.cpp
  engine/src/PlayTestsEntt.cpp
  engine/src/JoltPlay.cpp
  engine/src/SceneFile.cpp
  engine/src/SceneLoad.cpp
  engine/src/SceneWrite.cpp
  engine/src/Prefab.cpp
  engine/src/MeshBvh.cpp
  engine/src/MeshLoad.cpp
  engine/src/MeshObj.cpp
  engine/src/MeshCooked.cpp
  engine/src/Hash.cpp
  engine/src/CookSidecar.cpp
  engine/src/Content.cpp
  engine/src/TextureCooked.cpp
  engine/src/Catalog.cpp
  engine/src/Terrain.cpp
  engine/src/Stream.cpp
  engine/src/Frustum.cpp
  engine/src/GpuLights.cpp
  engine/src/EngineSettings.cpp
  engine/src/Role.cpp
)

set(RAYTRACER_IMPORT_SOURCES
  engine/src/MeshImport.cpp
)

set(RAYTRACER_RENDER_SOURCES
  engine/src/RayTracer.cpp
  engine/src/SelfTestEntt.cpp
  engine/src/image_write.cpp
  engine/src/image_read.cpp
  engine/src/GpuGl.cpp
  engine/src/GpuShaders.cpp
  engine/src/GpuShaderTrace.cpp
  engine/src/GpuRaster.cpp
  engine/src/GpuRayTracer.cpp
  engine/src/GpuRender.cpp
  engine/src/SceneDebug.cpp
)

set(RAYTRACER_AUDIO_SOURCES
  engine/src/Sound.cpp
  engine/src/Miniaudio.cpp
)

set(RAYTRACER_EDITOR_SOURCES
  editor/src/main.cpp
  editor/src/editor.cpp
  editor/src/EditorPlay.cpp
  editor/src/EditorScene.cpp
  editor/src/EditorHistory.cpp
  editor/src/EditorUi.cpp
  editor/src/EditorWidgets.cpp
  editor/src/DebugDraw.cpp
)

if(RAYTRACER_DILIGENT)
  list(APPEND RAYTRACER_EDITOR_SOURCES editor/src/EditorDiligent.cpp)
endif()

set(RAYTRACER_IMGUI_SOURCES
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl3.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
)

function(raytracer_set_engine_includes target_name)
  target_include_directories(${target_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/include"
    "${CMAKE_SOURCE_DIR}/third_party"
  )
  target_compile_definitions(${target_name} PUBLIC
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
  )
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /EHsc /bigobj /W4)
  endif()
endfunction()

function(raytracer_set_exe_output target_name)
  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
  )
  if(WIN32)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${target_name}>
        $<TARGET_FILE_DIR:${target_name}>
      COMMAND_EXPAND_LISTS
    )
  endif()
endfunction()

if(TARGET flatbuffers::flatc)
  set(RAYTRACER_FLATC flatbuffers::flatc)
else()
  find_program(RAYTRACER_FLATC NAMES flatc REQUIRED)
endif()

add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/generated/content_generated.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
  COMMAND ${RAYTRACER_FLATC} --cpp -o "${CMAKE_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/schemas/content.fbs"
  DEPENDS "${CMAKE_SOURCE_DIR}/schemas/content.fbs"
  COMMENT "flatc content.fbs"
)

# --- Libraries ----------------------------------------------------------------

add_library(raytracer_core STATIC ${RAYTRACER_CORE_SOURCES} "${CMAKE_BINARY_DIR}/generated/content_generated.h")
raytracer_set_engine_includes(raytracer_core)
target_include_directories(raytracer_core PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(raytracer_core PUBLIC
  enkiTS::enkiTS
  spdlog::spdlog
  Tracy::TracyClient
  EnTT::EnTT
  glm::glm
  Bitsery::bitsery
  unofficial::sqlite3::sqlite3
  Jolt::Jolt
  zstd::libzstd
  PhysFS::PhysFS
  flatbuffers::flatbuffers
  meshoptimizer::meshoptimizer
)
# Overlay builds Jolt with DOUBLE_PRECISION; keep the define on every core TU.
target_compile_definitions(raytracer_core PUBLIC JPH_DOUBLE_PRECISION)
if(RAYTRACER_SHIPPING)
  target_compile_definitions(raytracer_core PUBLIC RAYTRACER_SHIPPING=1)
endif()

add_library(raytracer_import STATIC ${RAYTRACER_IMPORT_SOURCES})
raytracer_set_engine_includes(raytracer_import)
target_include_directories(raytracer_import PRIVATE "${FBX_SDK_ROOT}/include")
target_link_libraries(raytracer_import PUBLIC raytracer_core)
set(FBX_SDK_LIB_DIR "${FBX_SDK_ROOT}/lib/x64/release")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND EXISTS "${FBX_SDK_ROOT}/lib/x64/debug/libfbxsdk-md.lib")
  set(FBX_SDK_LIB_DIR "${FBX_SDK_ROOT}/lib/x64/debug")
endif()
target_link_libraries(raytracer_import PRIVATE
  "${FBX_SDK_LIB_DIR}/libfbxsdk-md.lib"
  "${FBX_SDK_LIB_DIR}/libxml2-md.lib"
  "${FBX_SDK_LIB_DIR}/zlib-md.lib"
)

add_library(raytracer_render STATIC ${RAYTRACER_RENDER_SOURCES})
raytracer_set_engine_includes(raytracer_render)
target_link_libraries(raytracer_render PUBLIC raytracer_core raytracer_import)
target_link_libraries(raytracer_render PRIVATE opengl32)

add_library(raytracer_audio STATIC ${RAYTRACER_AUDIO_SOURCES})
raytracer_set_engine_includes(raytracer_audio)
target_link_libraries(raytracer_audio PUBLIC raytracer_core)
target_link_libraries(raytracer_audio PRIVATE miniaudio::miniaudio)

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
  raytracer_set_engine_includes(raytracer_rhi)
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
    SDL3::SDL3
  )
  target_include_directories(raytracer_rhi PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/src"
  )
  target_compile_definitions(raytracer_rhi PUBLIC RAYTRACER_DILIGENT=1)
  target_compile_definitions(raytracer_rhi PRIVATE
    "RAYTRACER_SHADER_DIR=\"${CMAKE_SOURCE_DIR}/engine/shaders\""
  )

  add_executable(diligent_smoke tools/diligent_smoke.cpp)
  raytracer_set_exe_output(diligent_smoke)
  target_link_libraries(diligent_smoke PRIVATE raytracer_rhi)
  if(WIN32)
    target_link_libraries(diligent_smoke PRIVATE user32 gdi32)
  endif()
endif()

# --- Executables --------------------------------------------------------------

add_executable(raytracer
  ${RAYTRACER_EDITOR_SOURCES}
  ${RAYTRACER_IMGUI_SOURCES}
)
raytracer_set_exe_output(raytracer)
target_include_directories(raytracer PRIVATE
  editor/include
  third_party/imgui
  third_party/imgui/backends
)
target_compile_definitions(raytracer PRIVATE
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
)
if(RAYTRACER_DILIGENT)
  target_compile_definitions(raytracer PRIVATE RAYTRACER_DILIGENT=1)
endif()
if(MSVC)
  target_compile_options(raytracer PRIVATE /EHsc /bigobj /W4)
  set_source_files_properties(
    ${RAYTRACER_IMGUI_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/W0"
  )
endif()

target_link_libraries(raytracer PRIVATE
  raytracer_core
  raytracer_import
  raytracer_render
  raytracer_audio
  SDL3::SDL3
  user32
  gdi32
  opengl32
  comdlg32
  advapi32
)
if(RAYTRACER_DILIGENT)
  target_link_libraries(raytracer PRIVATE raytracer_rhi)
endif()

# Headless doctest runner: core only (demo OBJ). No render, ImGui, OpenGL, or FBX.
add_executable(tests tests/main.cpp tests/SimReplayTests.cpp tests/SimSaveTests.cpp tests/TransformTests.cpp
  tests/MeshCookedTests.cpp tests/ContentTests.cpp tests/TerrainTests.cpp tests/StreamTests.cpp tests/FrustumTests.cpp)
raytracer_set_exe_output(tests)
target_compile_definitions(tests PRIVATE
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
)
if(MSVC)
  target_compile_options(tests PRIVATE /EHsc /bigobj /W4)
endif()

target_link_libraries(tests PRIVATE
  raytracer_core
  doctest::doctest
)

add_library(raytracer_cook STATIC
  cooker/src/CookMesh.cpp
  cooker/src/CookDeps.cpp
  cooker/src/CookTexture.cpp
  cooker/src/CookTables.cpp
  cooker/src/PackZip.cpp
)
raytracer_set_engine_includes(raytracer_cook)
target_include_directories(raytracer_cook PUBLIC "${CMAKE_SOURCE_DIR}/cooker/include")
target_link_libraries(raytracer_cook PUBLIC raytracer_core)
target_link_libraries(raytracer_cook PRIVATE
  assimp::assimp
  meshoptimizer::meshoptimizer
  Microsoft::DirectXTex
)
# flatc path for cooker tables (JSON → binary FlatBuffers).
set(_raytracer_flatc_path "")
if(TARGET flatbuffers::flatc)
  get_target_property(_raytracer_flatc_path flatbuffers::flatc IMPORTED_LOCATION)
  if(NOT _raytracer_flatc_path)
    get_target_property(_raytracer_flatc_path flatbuffers::flatc LOCATION)
  endif()
endif()
if(NOT _raytracer_flatc_path)
  find_program(_raytracer_flatc_path NAMES flatc)
endif()
if(NOT _raytracer_flatc_path)
  set(_raytracer_flatc_path "flatc")
endif()
file(TO_CMAKE_PATH "${_raytracer_flatc_path}" _raytracer_flatc_path)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/schemas" _raytracer_schemas_path)
# Escape backslashes for the string literal baked into CookTables.cpp.
string(REPLACE "\\" "\\\\" _raytracer_flatc_path "${_raytracer_flatc_path}")
string(REPLACE "\\" "\\\\" _raytracer_schemas_path "${_raytracer_schemas_path}")
target_compile_definitions(raytracer_cook PRIVATE
  "RAYTRACER_FLATC=\"${_raytracer_flatc_path}\""
  "RAYTRACER_SCHEMAS=\"${_raytracer_schemas_path}\""
)

add_executable(cooker cooker/src/main.cpp)
raytracer_set_exe_output(cooker)
target_link_libraries(cooker PRIVATE raytracer_cook)
if(MSVC)
  target_compile_options(cooker PRIVATE /EHsc /bigobj /W4)
endif()

if(MSVC)
  set_source_files_properties(
    engine/src/image_write.cpp
    engine/src/image_read.cpp
    engine/src/Miniaudio.cpp
    PROPERTIES COMPILE_OPTIONS "/W0"
  )
endif()
