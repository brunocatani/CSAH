cmake_minimum_required(VERSION 4.2)

set(NAME "fo4vr-community-shaders")
set(FRIENDLY_NAME "FO4VR Community Shaders")
set(VERSION 0.2.0)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(DEFINED VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE
    "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
  set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
  set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "")
else()
  message(FATAL_ERROR "VCPKG_ROOT is not set")
endif()

if(DEFINED COMMON_LIB_F4VR_PATH AND
    NOT COMMON_LIB_F4VR_PATH STREQUAL "")
  set(COMMON_LIB_F4VR_PATH_SELECTED
    "${COMMON_LIB_F4VR_PATH}"
    CACHE PATH "" FORCE)
else()
  set(COMMON_LIB_F4VR_PATH_SELECTED
    "${WORKSPACE_ROOT}/libraries_and_tools/CommonLibF4VR"
    CACHE PATH "" FORCE)
endif()
if(NOT EXISTS
    "${COMMON_LIB_F4VR_PATH_SELECTED}/CommonLibF4/include/REL/Relocation.h")
  message(FATAL_ERROR
    "CommonLibF4VR path not found in '${COMMON_LIB_F4VR_PATH_SELECTED}'")
endif()

set(ROCK_SDK_PATH_SELECTED
  "${WORKSPACE_ROOT}/main_projects/ROCK/SDK/ROCK/include"
  CACHE PATH "ROCK provider SDK include directory")
if(NOT EXISTS "${ROCK_SDK_PATH_SELECTED}/ROCKProviderApi.h")
  message(FATAL_ERROR
    "ROCK provider SDK not found in '${ROCK_SDK_PATH_SELECTED}'")
endif()

set(PRISMA_F4VR_API_PATH_SELECTED
  "${WORKSPACE_ROOT}/secondary_projects/Ported_mods/framework-F4VR-Conversion/ports/fo4vr/include"
  CACHE PATH "PrismaUI FO4VR public API include directory")
if(NOT EXISTS
    "${PRISMA_F4VR_API_PATH_SELECTED}/PrismaUI_F4VR_API.h")
  message(FATAL_ERROR
    "PrismaUI FO4VR public API not found in '${PRISMA_F4VR_API_PATH_SELECTED}'")
endif()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)

include("${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake")
fo4vr_cs_verify_fo4vr_loader("${SOURCE_DIR}/XSEPlugin.cpp")
set(RENDERER_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp")
include("${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake")
set(WRIST_PANEL_SOURCE "${SOURCE_DIR}/ui/WristPanelRuntime.cpp")
set(WRIST_PANEL_POSE_SOURCE "${SOURCE_DIR}/ui/WristPanelPose.h")
set(D3D11_HOOK_SOURCE "${SOURCE_DIR}/render/D3D11Hooks.cpp")
set(LINEAR_LIGHTING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
include("${ROOT_DIR}/cmake/VerifyWristPanel.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  "${RENDERER_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  "${WRIST_PANEL_SOURCE}"
  "${WRIST_PANEL_POSE_SOURCE}"
  "${D3D11_HOOK_SOURCE}"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyWristPanel.cmake")

add_custom_target(VerifyFo4VrLoader ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLOADER_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp"
    -P "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  COMMENT "Verifying FO4VR F4SE loader compatibility contract"
  VERBATIM)

add_custom_target(VerifyFo4VrRendererHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  COMMENT "Verifying FO4VR renderer hook identity contract"
  VERBATIM)

add_custom_target(VerifyWristPanel ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DWRIST_PANEL_SOURCE=${WRIST_PANEL_SOURCE}"
    "-DWRIST_PANEL_POSE_SOURCE=${WRIST_PANEL_POSE_SOURCE}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyWristPanel.cmake"
  COMMENT "Verifying optional wrist-panel provider contracts"
  VERBATIM)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)

add_subdirectory(
  "${COMMON_LIB_F4VR_PATH_SELECTED}"
  "CommonLibF4"
  EXCLUDE_FROM_ALL)

add_library(FO4VRCommunityShadersCore STATIC
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.h"
)
target_compile_features(FO4VRCommunityShadersCore PUBLIC cxx_std_23)
target_include_directories(FO4VRCommunityShadersCore PUBLIC "${SOURCE_DIR}")
target_compile_options(FO4VRCommunityShadersCore PRIVATE
  /sdl
  /utf-8
  /permissive-
  /Zc:__cplusplus
  /Zc:preprocessor
  /W4
)

add_library(${PROJECT_NAME} SHARED
  "${SOURCE_DIR}/PCH.h"
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${SOURCE_DIR}/exports.def"
  "${SOURCE_DIR}/resources.h"
  "${SOURCE_DIR}/resources.rc"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.h"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.h"
  "${SOURCE_DIR}/render/D3D11HookRepairGate.h"
  "${D3D11_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/D3D11Hooks.h"
  "${SOURCE_DIR}/support/Logger.h"
  "${SOURCE_DIR}/ui/LinearLightingTelemetryGate.h"
  "${SOURCE_DIR}/ui/PointerClickGate.h"
  "${SOURCE_DIR}/ui/WristPanelPose.h"
  "${SOURCE_DIR}/ui/WristPanelRuntime.cpp"
  "${SOURCE_DIR}/ui/WristPanelRuntime.h"
  "${SOURCE_DIR}/ui/WristProviderRetry.h"
)
add_dependencies(${PROJECT_NAME}
  VerifyFo4VrLoader
  VerifyFo4VrRendererHook
  VerifyWristPanel)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  NOMMNOSOUND
)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${SOURCE_DIR}"
  "${ROCK_SDK_PATH_SELECTED}"
  "${PRISMA_F4VR_API_PATH_SELECTED}"
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
  FO4VRCommunityShadersCore
  CommonLibF4::CommonLibF4
  nlohmann_json::nlohmann_json
  spdlog::spdlog
)

target_precompile_headers(${PROJECT_NAME} PRIVATE "${SOURCE_DIR}/PCH.h")
target_compile_options(${PROJECT_NAME} PRIVATE
  /sdl
  /utf-8
  /Zi
  /permissive-
  /Zc:__cplusplus
  /Zc:preprocessor
  /W4
  /wd4715
  "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
)
target_link_options(${PROJECT_NAME} PRIVATE
  "/DEF:${SOURCE_DIR}/exports.def"
  "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
  "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE
  "${CMAKE_CXX_FLAGS_RELEASE}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/PrismaUI_F4/views/FO4VR-Community-Shaders"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${ROOT_DIR}/view"
    "${ROOT_DIR}/package/PrismaUI_F4/views/FO4VR-Community-Shaders"
  COMMENT "Staging ${PROJECT_NAME} in the local package"
)

option(POST_BUILD_COPY_PLUGIN
  "Copy Community Shaders to a mod root." OFF)
set(COPY_PLUGIN_BASE_PATH "" CACHE STRING
  "Semicolon-separated Community Shaders mod roots.")
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(FATAL_ERROR
      "COPY_PLUGIN_BASE_PATH is required when POST_BUILD_COPY_PLUGIN is enabled")
  endif()
  foreach(copy_path IN LISTS COPY_PLUGIN_BASE_PATH)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/PrismaUI_F4/views/FO4VR-Community-Shaders"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${ROOT_DIR}/view"
        "${copy_path}/PrismaUI_F4/views/FO4VR-Community-Shaders"
      COMMENT "Deploying ${PROJECT_NAME} to '${copy_path}'"
    )
  endforeach()
endif()

option(BUILD_FO4VR_CS_TESTS
  "Build focused FO4VR Community Shaders tests." OFF)
if(BUILD_FO4VR_CS_TESTS)
  enable_testing()
  find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  add_executable(FO4VRCommunityShadersLinearLightingSettingsTests
    "${ROOT_DIR}/tests/LinearLightingSettingsTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersLinearLightingSettingsTests PRIVATE cxx_std_23)
  target_compile_options(
    FO4VRCommunityShadersLinearLightingSettingsTests PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
  )
  target_link_libraries(
    FO4VRCommunityShadersLinearLightingSettingsTests PRIVATE
    FO4VRCommunityShadersCore)
  add_test(NAME FO4VRCommunityShaders.LinearLightingSettings
    COMMAND FO4VRCommunityShadersLinearLightingSettingsTests)

  add_test(NAME FO4VRCommunityShaders.LoaderSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DLOADER_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake'")
  add_test(NAME FO4VRCommunityShaders.RendererHookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake'")
  add_test(NAME FO4VRCommunityShaders.WristPanelSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DWRIST_PANEL_SOURCE=${WRIST_PANEL_SOURCE}' '-DWRIST_PANEL_POSE_SOURCE=${WRIST_PANEL_POSE_SOURCE}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyWristPanel.cmake'")

  add_executable(FO4VRCommunityShadersPointerClickGateTests
    "${ROOT_DIR}/tests/PointerClickGateTests.cpp"
    "${SOURCE_DIR}/ui/PointerClickGate.h")
  target_compile_features(
    FO4VRCommunityShadersPointerClickGateTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersPointerClickGateTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersPointerClickGateTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.PointerClickGate
    COMMAND FO4VRCommunityShadersPointerClickGateTests)

  add_executable(FO4VRCommunityShadersD3D11HookRepairGateTests
    "${ROOT_DIR}/tests/D3D11HookRepairGateTests.cpp"
    "${SOURCE_DIR}/render/D3D11HookRepairGate.h")
  target_compile_features(
    FO4VRCommunityShadersD3D11HookRepairGateTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersD3D11HookRepairGateTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersD3D11HookRepairGateTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.D3D11HookRepairGate
    COMMAND FO4VRCommunityShadersD3D11HookRepairGateTests)

  add_executable(FO4VRCommunityShadersWristProviderRetryTests
    "${ROOT_DIR}/tests/WristProviderRetryTests.cpp"
    "${SOURCE_DIR}/ui/WristProviderRetry.h")
  target_compile_features(
    FO4VRCommunityShadersWristProviderRetryTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersWristProviderRetryTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersWristProviderRetryTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.WristProviderRetry
    COMMAND FO4VRCommunityShadersWristProviderRetryTests)

  add_executable(FO4VRCommunityShadersWristPanelPoseTests
    "${ROOT_DIR}/tests/WristPanelPoseTests.cpp"
    "${SOURCE_DIR}/ui/WristPanelPose.h")
  target_compile_features(
    FO4VRCommunityShadersWristPanelPoseTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersWristPanelPoseTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersWristPanelPoseTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.WristPanelPose
    COMMAND FO4VRCommunityShadersWristPanelPoseTests)

  add_executable(FO4VRCommunityShadersLinearLightingTelemetryGateTests
    "${ROOT_DIR}/tests/LinearLightingTelemetryGateTests.cpp"
    "${SOURCE_DIR}/ui/LinearLightingTelemetryGate.h")
  target_compile_features(
    FO4VRCommunityShadersLinearLightingTelemetryGateTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersLinearLightingTelemetryGateTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersLinearLightingTelemetryGateTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.LinearLightingTelemetryGate
    COMMAND FO4VRCommunityShadersLinearLightingTelemetryGateTests)

  add_test(NAME FO4VRCommunityShaders.LinearLightingShader
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/verify_linear_lighting_shader.py"
      --root "${ROOT_DIR}")
  add_test(NAME FO4VRCommunityShaders.LinearLightingShaderCensus
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
      --input "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --known-dir "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --expect-total 22
      --expect-unique 22
      --expect-bslighting-shape 22
      --expect-known 22)
endif()
