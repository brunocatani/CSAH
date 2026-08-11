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
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include("${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake")
fo4vr_cs_verify_fo4vr_loader("${SOURCE_DIR}/XSEPlugin.cpp")
set(RENDERER_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp")
include("${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake")
set(WRIST_PANEL_SOURCE "${SOURCE_DIR}/ui/WristPanelRuntime.cpp")
set(WRIST_PANEL_POSE_SOURCE "${SOURCE_DIR}/ui/WristPanelPose.h")
set(WRIST_PANEL_VIEW_SOURCE
  "${ROOT_DIR}/view/index.html")
set(D3D11_HOOK_SOURCE "${SOURCE_DIR}/render/D3D11Hooks.cpp")
set(LINEAR_LIGHTING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
set(LINEAR_LIGHTING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h")
set(POINT_LIGHT_HOOK_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightHook.cpp")
set(QUALIFICATION_SOURCE
  "${SOURCE_DIR}/diagnostics/LinearLightingQualification.cpp")
set(PLUGIN_SOURCE "${SOURCE_DIR}/XSEPlugin.cpp")
set(DFLIGHT_AMBIENT_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/DFLightAmbientContracts.json")
set(DFLIGHT_AMBIENT_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_dflight_ambient_contracts.py")
set(DFLIGHT_AMBIENT_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedDFLightAmbientContracts.inl")
set(DFLIGHT_AMBIENT_PATCH_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
file(GLOB DFLIGHT_AMBIENT_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedDFLightAmbient/*.dxbc")
set(SKY_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLightingContracts.json")
set(SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_sky_linear_lighting_contracts.py")
set(SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedSkyLinearLightingContracts.inl")
set(SKY_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLighting/SkyLinearLighting.hlsl")
set(RESOURCE_SOURCE "${SOURCE_DIR}/resources.rc")
file(GLOB SKY_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLighting/*.dxbc")
file(GLOB SKY_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedSkyLinearLighting/*.dxbc")
set(DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLightingContracts.json")
set(DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_distant_tree_linear_lighting_contracts.py")
set(DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedDistantTreeLinearLightingContract.inl")
set(DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLighting/DistantTreeLinearLighting.hlsl")
set(FIXED_SHADER_BINDING_LOOKUP_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/FixedShaderBindingLookup.h")
file(GLOB DISTANT_TREE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLighting/*.dxbc")
file(GLOB DISTANT_TREE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedDistantTreeLinearLighting/*.dxbc")
set(PARTICLE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLightingContracts.json")
set(PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_particle_linear_lighting_contracts.py")
set(PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedParticleLinearLightingContracts.inl")
set(PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLighting/ParticleLinearLighting.hlsl")
file(GLOB PARTICLE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLighting/*.dxbc")
file(GLOB PARTICLE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedParticleLinearLighting/*.dxbc")
set(WATER_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLightingContracts.json")
set(WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_water_linear_lighting_contracts.py")
set(WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedWaterLinearLightingContracts.inl")
set(WATER_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLighting/WaterColorTransformTemplate.hlsl")
file(GLOB WATER_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLighting/*.dxbc")
file(GLOB WATER_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedWaterLinearLighting/*.dxbc")
set(EFFECT_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLightingContracts.json")
set(EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_effect_linear_lighting_contracts.py")
set(EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedEffectLinearLightingContracts.inl")
set(EFFECT_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLighting/EffectLinearLighting.hlsl")
file(GLOB EFFECT_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLighting/*.dxbc")
file(GLOB EFFECT_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedEffectLinearLighting/*.dxbc")
include("${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake")
include("${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake")
include("${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake")
include("${ROOT_DIR}/cmake/VerifyWristPanel.cmake")
include("${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake")
include("${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake")
include("${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  "${RENDERER_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  "${ROOT_DIR}/cmake/TestVerifyFo4VrRendererHookNegative.cmake"
  "${WRIST_PANEL_SOURCE}"
  "${WRIST_PANEL_POSE_SOURCE}"
  "${WRIST_PANEL_VIEW_SOURCE}"
  "${D3D11_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  "${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${LINEAR_LIGHTING_RUNTIME_HEADER}"
  "${POINT_LIGHT_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake"
  "${ROOT_DIR}/cmake/VerifyWristPanel.cmake"
  "${QUALIFICATION_SOURCE}"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingContractMask.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h"
  "${DFLIGHT_AMBIENT_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h"
  "${DFLIGHT_AMBIENT_CONTRACT_MANIFEST}"
  "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
  "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
  ${DFLIGHT_AMBIENT_SHADER_ASSETS}
  "${SKY_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${SKY_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${SKY_LINEAR_LIGHTING_SHADER_ASSETS}
  ${SKY_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE}"
  "${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
  ${DISTANT_TREE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${DISTANT_TREE_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${PARTICLE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${PARTICLE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${PARTICLE_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${WATER_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${WATER_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${WATER_LINEAR_LIGHTING_SHADER_ASSETS}
  ${WATER_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${EFFECT_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${EFFECT_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${EFFECT_LINEAR_LIGHTING_SHADER_ASSETS}
  ${EFFECT_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${SOURCE_DIR}/support/NearAllocation.h"
  "${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake"
  "${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake"
  "${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake")

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
    "-DWRIST_PANEL_VIEW_SOURCE=${WRIST_PANEL_VIEW_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyWristPanel.cmake"
  COMMENT "Verifying optional wrist-panel provider contracts"
  VERBATIM)

add_custom_target(VerifyD3D11Hooks ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  COMMENT "Verifying D3D11 method-detour ownership contract"
  VERBATIM)

add_custom_target(VerifyLinearLightingConstantScope ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake"
  COMMENT "Verifying Linear Lighting constant-buffer restoration contract"
  VERBATIM)

add_custom_target(VerifyDFTiledPointLightHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPOINT_LIGHT_HOOK_SOURCE=${POINT_LIGHT_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake"
  COMMENT "Verifying FO4VR DFTiled point-light producer contract"
  VERBATIM)

add_custom_target(VerifyLinearLightingQualification ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPLUGIN_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DQUALIFICATION_SOURCE=${QUALIFICATION_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake"
  COMMENT "Verifying unattended Linear Lighting qualification contract"
  VERBATIM)

add_custom_target(VerifyDFLightAmbientReplacement ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DDFLIGHT_AMBIENT_PATCH_HEADER=${DFLIGHT_AMBIENT_PATCH_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake"
  COMMENT "Verifying exact DFLight ambient replacement contract"
  VERBATIM)

add_custom_target(VerifySkyLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DSKY_LINEAR_LIGHTING_SHADER_SOURCE=${SKY_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake"
  COMMENT "Verifying frame-only Sky Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyDistantTreeLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DDISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE=${DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake"
  COMMENT "Verifying frame-only DistantTree Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyParticleLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DPARTICLE_LINEAR_LIGHTING_SHADER_SOURCE=${PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake"
  COMMENT "Verifying frame-only Particle Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyWaterLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DWATER_LINEAR_LIGHTING_SHADER_SOURCE=${WATER_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake"
  COMMENT "Verifying frame-only Water Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyEffectLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DEFFECT_LINEAR_LIGHTING_SHADER_SOURCE=${EFFECT_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake"
  COMMENT "Verifying frame-only Effect Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyFixedShaderBindingLookup ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DFIXED_SHADER_BINDING_LOOKUP_HEADER=${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
    -P "${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake"
  COMMENT "Verifying allocation-free shader-binding lookup"
  VERBATIM)

set(LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/LinearLightingContracts.json")
set(LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_linear_lighting_runtime_contracts.py")
set(LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedLinearLightingContracts.inl")
add_custom_target(VerifyLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying generated Linear Lighting shader identities"
  VERBATIM)

add_custom_target(VerifyDFLightAmbientContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying generated DFLight ambient shader identities"
  VERBATIM)

add_custom_target(VerifySkyLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Sky Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyDistantTreeLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
    --check
  COMMENT "Verifying FO4VR DistantTree Linear Lighting contract"
  VERBATIM)

add_custom_target(VerifyParticleLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Particle Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyWaterLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Water Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyEffectLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Effect Linear Lighting contracts"
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
  "${QUALIFICATION_SOURCE}"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualification.h"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingContractMask.h"
  "${POINT_LIGHT_HOOK_SOURCE}"
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightHook.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h"
  "${DFLIGHT_AMBIENT_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h"
  "${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
  "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.h"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.h"
  "${D3D11_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/D3D11Hooks.h"
  "${SOURCE_DIR}/support/Logger.h"
  "${SOURCE_DIR}/support/NearAllocation.h"
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
  VerifyD3D11Hooks
  VerifyDistantTreeLinearLighting
  VerifyDistantTreeLinearLightingContracts
  VerifyParticleLinearLighting
  VerifyParticleLinearLightingContracts
  VerifyWaterLinearLighting
  VerifyWaterLinearLightingContracts
  VerifyEffectLinearLighting
  VerifyEffectLinearLightingContracts
  VerifyDFLightAmbientReplacement
  VerifyDFLightAmbientContracts
  VerifyDFTiledPointLightHook
  VerifyFixedShaderBindingLookup
  VerifyLinearLightingConstantScope
  VerifyLinearLightingContracts
  VerifyLinearLightingQualification
  VerifySkyLinearLighting
  VerifySkyLinearLightingContracts
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
find_package(minhook CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
  FO4VRCommunityShadersCore
  CommonLibF4::CommonLibF4
  minhook::minhook
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

  add_executable(FO4VRCommunityShadersFixedShaderBindingLookupTests
    "${ROOT_DIR}/tests/FixedShaderBindingLookupTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersFixedShaderBindingLookupTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersFixedShaderBindingLookupTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersFixedShaderBindingLookupTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.FixedShaderBindingLookup
    COMMAND FO4VRCommunityShadersFixedShaderBindingLookupTests)

  add_test(NAME FO4VRCommunityShaders.LoaderSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DLOADER_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake'")
  add_test(NAME FO4VRCommunityShaders.RendererHookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake'")
  add_test(NAME FO4VRCommunityShaders.RendererHookValidatorNegative
    COMMAND "${CMAKE_COMMAND}"
      "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
      "-DRENDERER_VALIDATOR=${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
      "-DTEST_OUTPUT_DIR=${BUILD_DIR}/renderer-validator-negative"
      -P "${ROOT_DIR}/cmake/TestVerifyFo4VrRendererHookNegative.cmake")
  add_test(NAME FO4VRCommunityShaders.WristPanelSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DWRIST_PANEL_SOURCE=${WRIST_PANEL_SOURCE}' '-DWRIST_PANEL_POSE_SOURCE=${WRIST_PANEL_POSE_SOURCE}' '-DWRIST_PANEL_VIEW_SOURCE=${WRIST_PANEL_VIEW_SOURCE}' '-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyWristPanel.cmake'")

  add_test(NAME FO4VRCommunityShaders.D3D11HookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake'")

  add_test(NAME FO4VRCommunityShaders.LinearLightingConstantScopeSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake'")

  add_test(NAME FO4VRCommunityShaders.DFTiledPointLightHookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DPOINT_LIGHT_HOOK_SOURCE=${POINT_LIGHT_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake'")

  add_test(NAME FO4VRCommunityShaders.LinearLightingQualificationSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DPLUGIN_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DQUALIFICATION_SOURCE=${QUALIFICATION_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake'")

  add_executable(FO4VRCommunityShadersD3D11MethodDetourSmokeTests
    "${ROOT_DIR}/tests/D3D11MethodDetourSmokeTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersD3D11MethodDetourSmokeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersD3D11MethodDetourSmokeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersD3D11MethodDetourSmokeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersD3D11MethodDetourSmokeTests PRIVATE
    minhook::minhook d3d11)
  add_test(NAME FO4VRCommunityShaders.D3D11MethodDetourSmoke
    COMMAND FO4VRCommunityShadersD3D11MethodDetourSmokeTests)

  add_executable(FO4VRCommunityShadersLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/LinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersLinearLightingShaderParityTests PRIVATE
    d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.LinearLightingShaderParity
    COMMAND FO4VRCommunityShadersLinearLightingShaderParityTests
      "${ROOT_DIR}")

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

  add_executable(FO4VRCommunityShadersLinearLightingQualificationTests
    "${ROOT_DIR}/tests/LinearLightingQualificationTests.cpp"
    "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h")
  target_compile_features(
    FO4VRCommunityShadersLinearLightingQualificationTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersLinearLightingQualificationTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersLinearLightingQualificationTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.LinearLightingQualification
    COMMAND FO4VRCommunityShadersLinearLightingQualificationTests)

  add_executable(FO4VRCommunityShadersDFLightAmbientShaderPatchTests
    "${ROOT_DIR}/tests/DFLightAmbientShaderPatchTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
  target_compile_features(
    FO4VRCommunityShadersDFLightAmbientShaderPatchTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersDFLightAmbientShaderPatchTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersDFLightAmbientShaderPatchTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.DFLightAmbientShaderPatch
    COMMAND FO4VRCommunityShadersDFLightAmbientShaderPatchTests)

  add_executable(FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests
    "${ROOT_DIR}/tests/DFLightAmbientShaderBytecodeTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
  target_compile_features(
    FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests PRIVATE d3d11)
  add_test(NAME FO4VRCommunityShaders.DFLightAmbientShaderBytecode
    COMMAND FO4VRCommunityShadersDFLightAmbientShaderBytecodeTests
      "${ROOT_DIR}")

  add_executable(FO4VRCommunityShadersDFTiledPointLightModelTests
    "${ROOT_DIR}/tests/DFTiledPointLightModelTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightModel.h")
  target_compile_features(
    FO4VRCommunityShadersDFTiledPointLightModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersDFTiledPointLightModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersDFTiledPointLightModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersDFTiledPointLightModelTests PRIVATE
    FO4VRCommunityShadersCore)
  add_test(NAME FO4VRCommunityShaders.DFTiledPointLightModel
    COMMAND FO4VRCommunityShadersDFTiledPointLightModelTests)

  add_executable(FO4VRCommunityShadersDFLightProducerModelTests
    "${ROOT_DIR}/tests/DFLightProducerModelTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h")
  target_compile_features(
    FO4VRCommunityShadersDFLightProducerModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersDFLightProducerModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersDFLightProducerModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersDFLightProducerModelTests PRIVATE
    FO4VRCommunityShadersCore)
  add_test(NAME FO4VRCommunityShaders.DFLightProducerModel
    COMMAND FO4VRCommunityShadersDFLightProducerModelTests)

  add_test(NAME FO4VRCommunityShaders.LinearLightingShader
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/verify_linear_lighting_shader.py"
      --root "${ROOT_DIR}")
  add_test(NAME FO4VRCommunityShaders.LinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME FO4VRCommunityShaders.DFLightAmbientRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME FO4VRCommunityShaders.SkyLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME FO4VRCommunityShaders.DistantTreeLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
      --check)
  add_test(NAME FO4VRCommunityShaders.ParticleLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME FO4VRCommunityShaders.WaterLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME FO4VRCommunityShaders.EffectLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_executable(FO4VRCommunityShadersSkyLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/SkyLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersSkyLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersSkyLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersSkyLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersSkyLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersSkyLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.SkyLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersSkyLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/DistantTreeLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.DistantTreeLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersDistantTreeLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(FO4VRCommunityShadersParticleLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/ParticleLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersParticleLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersParticleLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersParticleLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersParticleLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersParticleLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.ParticleLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersParticleLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(FO4VRCommunityShadersWaterLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/WaterLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersWaterLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersWaterLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersWaterLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersWaterLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersWaterLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.WaterLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersWaterLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(FO4VRCommunityShadersEffectLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/EffectLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersEffectLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersEffectLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersEffectLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersEffectLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersEffectLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.EffectLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersEffectLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_test(NAME FO4VRCommunityShaders.LinearLightingShaderCensus
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
      --input "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --known-dir "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --expect-total 288
      --expect-unique 288
      --expect-target-shape 288
      --expect-known 288)
  add_test(NAME FO4VRCommunityShaders.LinearLightingFxpParser
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tests/test_census_linear_lighting_fxp.py")
endif()
