cmake_minimum_required(VERSION 4.2)

set(NAME "fo4vr-community-shaders")
set(FRIENDLY_NAME "FO4VR Community Shaders")
set(VERSION 0.2.0)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)

set(DEVMENU_PACKAGE_ID "fo4vr-community-shaders")
set(DEVMENU_MANIFEST_SOURCE
  "${ROOT_DIR}/devmenu/${DEVMENU_PACKAGE_ID}/menu.json")
if(NOT EXISTS "${DEVMENU_MANIFEST_SOURCE}")
  message(FATAL_ERROR
    "Community Shaders owns its DevMenu manifest, but it is missing: "
    "'${DEVMENU_MANIFEST_SOURCE}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${DEVMENU_MANIFEST_SOURCE}")

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

set_from_environment(STREAMLINE_SDK_ROOT)
if(NOT DEFINED STREAMLINE_SDK_ROOT OR STREAMLINE_SDK_ROOT STREQUAL "")
  set(STREAMLINE_SDK_ROOT
    "${WORKSPACE_ROOT}/../devtools/streamline-sdk-v2.12.0"
    CACHE PATH "NVIDIA Streamline 2.12 SDK root")
endif()
set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_SDK_ROOT}/include")
set(STREAMLINE_RUNTIME_DIR "${STREAMLINE_SDK_ROOT}/bin/x64")
set(STREAMLINE_RUNTIME_FILES
  "${STREAMLINE_RUNTIME_DIR}/sl.interposer.dll"
  "${STREAMLINE_RUNTIME_DIR}/sl.common.dll"
  "${STREAMLINE_RUNTIME_DIR}/sl.dlss.dll"
  "${STREAMLINE_RUNTIME_DIR}/nvngx_dlss.dll")
set(STREAMLINE_LICENSE_FILES
  "${STREAMLINE_SDK_ROOT}/license.txt"
  "${STREAMLINE_SDK_ROOT}/3rd-party-licenses.md"
  "${STREAMLINE_RUNTIME_DIR}/nvngx_dlss.license.txt")
foreach(required IN ITEMS
    "${STREAMLINE_INCLUDE_DIR}/sl.h"
    ${STREAMLINE_RUNTIME_FILES}
    ${STREAMLINE_LICENSE_FILES})
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR
      "Required Streamline 2.12 file not found: '${required}'")
  endif()
endforeach()

list(GET STREAMLINE_RUNTIME_FILES 0 STREAMLINE_INTERPOSER_FILE)
list(GET STREAMLINE_RUNTIME_FILES 1 STREAMLINE_COMMON_FILE)
list(GET STREAMLINE_RUNTIME_FILES 2 STREAMLINE_DLSS_FILE)
list(GET STREAMLINE_RUNTIME_FILES 3 STREAMLINE_NGX_FILE)
file(SHA256 "${STREAMLINE_INTERPOSER_FILE}" STREAMLINE_INTERPOSER_SHA256)
file(SHA256 "${STREAMLINE_COMMON_FILE}" STREAMLINE_COMMON_SHA256)
file(SHA256 "${STREAMLINE_DLSS_FILE}" STREAMLINE_DLSS_SHA256)
file(SHA256 "${STREAMLINE_NGX_FILE}" STREAMLINE_NGX_SHA256)

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

set(DLAA_GENERATED_DIR "${BUILD_DIR}/generated/dlaa")
file(MAKE_DIRECTORY "${DLAA_GENERATED_DIR}")
configure_file(
  "${ROOT_DIR}/cmake/StreamlinePayloadHashes.in.h"
  "${DLAA_GENERATED_DIR}/GeneratedStreamlinePayloadHashes.h"
  @ONLY)

include("${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake")
fo4vr_cs_verify_fo4vr_loader("${SOURCE_DIR}/XSEPlugin.cpp")
set(RENDERER_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp")
set(DFPREPASS_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSDFPrePassShaderHook.cpp")
include("${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake")
set(SETTINGS_PATH_SOURCE "${SOURCE_DIR}/support/SettingsPath.cpp")
set(SETTINGS_PATH_HEADER "${SOURCE_DIR}/support/SettingsPath.h")
set(SHARED_SETTINGS_RUNTIME_SOURCE
  "${SOURCE_DIR}/settings/SharedSettingsRuntime.cpp")
set(SHARED_SETTINGS_RUNTIME_HEADER
  "${SOURCE_DIR}/settings/SharedSettingsRuntime.h")
set(DEVMENU_VERIFY_SCRIPT "${ROOT_DIR}/cmake/VerifyDevMenu.cmake")
set(PROJECT_CMAKE_SOURCE "${ROOT_DIR}/CMakeLists.txt")
set(SHARED_SETTINGS_SOURCE "${SHARED_SETTINGS_RUNTIME_SOURCE}")
set(SHARED_SETTINGS_HEADER "${SHARED_SETTINGS_RUNTIME_HEADER}")
set(SOURCE_ROOT "${ROOT_DIR}")
set(D3D11_HOOK_SOURCE "${SOURCE_DIR}/render/D3D11Hooks.cpp")
set(DLAA_SOURCE_DIR "${SOURCE_DIR}/Features/dlaa")
set(DLAA_RUNTIME_SOURCE "${DLAA_SOURCE_DIR}/DlaaRuntime.cpp")
set(DLAA_FRAME_HEADER "${DLAA_SOURCE_DIR}/Fo4VrFrameBuffer.h")
set(DLAA_ENGINE_HOOK_SOURCE "${DLAA_SOURCE_DIR}/DlaaEngineHooks.cpp")
set(DLAA_D3D11_HOOK_SOURCE "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.cpp")
set(DLAA_STREAMLINE_SOURCE "${DLAA_SOURCE_DIR}/StreamlineBackend.cpp")
set(DLAA_SETTINGS_STORE_SOURCE "${DLAA_SOURCE_DIR}/DlaaSettingsStore.cpp")
set(UPSCALING_COMPOSE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/ComposeUpscalingCS.hlsl")
set(UPSCALING_COMPOSE_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/ComposeUpscalingCS.dxbc")
set(UPSCALING_COMPOSE_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingComposeShader.h")
set(UPSCALING_MOTION_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/RepairMotionVectorsCS.hlsl")
set(UPSCALING_MOTION_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/RepairMotionVectorsCS.dxbc")
set(UPSCALING_MOTION_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingMotionRepairShader.h")
set(UPSCALING_REACTIVE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/BuildReactiveMaskCS.hlsl")
set(UPSCALING_REACTIVE_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/BuildReactiveMaskCS.dxbc")
set(UPSCALING_REACTIVE_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingReactiveMaskShader.h")
set_source_files_properties(
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(DLAA_VERIFY_SCRIPT "${ROOT_DIR}/cmake/VerifyDlaa.cmake")
set(DLAA_ENGINE_SOURCE "${DLAA_ENGINE_HOOK_SOURCE}")
set(DLAA_D3D_SOURCE "${DLAA_D3D11_HOOK_SOURCE}")
set(DLAA_SETTINGS_SOURCE "${DLAA_SETTINGS_STORE_SOURCE}")
set(RENDERER_D3D_SOURCE "${D3D11_HOOK_SOURCE}")
set_source_files_properties("${DLAA_STREAMLINE_SOURCE}" PROPERTIES
  SKIP_PRECOMPILE_HEADERS ON)
set(VANILLA_FIXES_SOURCE_DIR
  "${SOURCE_DIR}/Features/vanilla_fixes")
set(VANILLA_REFLECTION_PATCH_TOOL_SOURCE
  "${ROOT_DIR}/tools/VanillaReflectionPatchTool.cpp")
set(VANILLA_REFLECTION_PATCH_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.cpp")
set(VANILLA_REFLECTION_PATCH_HEADER
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.h")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/DirectionalLightDiagnosticSurface.cpp")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_HEADER
  "${VANILLA_FIXES_SOURCE_DIR}/DirectionalLightDiagnosticSurface.h")
set(VANILLA_SAO_BLUR_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SaoBlurHCS.hlsl")
set(VANILLA_SAO_RAW_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SaoRawAOCS.hlsl")
set(VANILLA_SSLR_BLUR_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrBlurHVS.hlsl")
set(VANILLA_SSLR_PREPASS_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrPrepassPS.hlsl")
set(VANILLA_SSLR_RAYTRACE_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrRaytracePS.hlsl")
set_source_files_properties(
  "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(VANILLA_SHADER_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.cpp")
set(VANILLA_FOCUS_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.cpp")
set(VANILLA_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.cpp")
set(VANILLA_SETTINGS_STORE_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.cpp")
set(VANILLA_SSLR_ENVIRONMENT_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.cpp")
set(VANILLA_IBL_GENERATOR_SOURCE
  "${ROOT_DIR}/tools/generate_ibl_material_contracts.py")
set(VANILLA_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyVanillaFixes.cmake")
set(NATIVE_SHADOW_SOURCE_DIR
  "${SOURCE_DIR}/Features/native_shadows")
set(NATIVE_SHADOW_RUNTIME_SOURCE
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowRuntime.cpp")
set(NATIVE_SHADOW_RUNTIME_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowRuntime.h")
set(NATIVE_SHADOW_SETTINGS_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettings.h")
set(NATIVE_SHADOW_SETTINGS_STORE_SOURCE
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettingsStore.cpp")
set(NATIVE_SHADOW_SETTINGS_STORE_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettingsStore.h")
set(NATIVE_SHADOW_PATCH_MODEL
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowPatchModel.h")
set(NATIVE_SHADOW_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyNativeShadows.cmake")
set_source_files_properties(
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(LINEAR_LIGHTING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
set(LINEAR_LIGHTING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h")
set(POINT_LIGHT_HOOK_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightHook.cpp")
set(QUALIFICATION_SOURCE
  "${SOURCE_DIR}/diagnostics/LinearLightingQualification.cpp")
set(PLUGIN_SOURCE "${SOURCE_DIR}/XSEPlugin.cpp")
set(VANILLA_D3D11_HOOK_SOURCE "${D3D11_HOOK_SOURCE}")
set(VANILLA_PLUGIN_SOURCE "${PLUGIN_SOURCE}")
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
set(VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLightingContracts.json")
set(VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_vls_composite_linear_lighting_contract.py")
set(VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedVLSCompositeLinearLightingContract.inl")
set(VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLighting/VLSCompositeLinearLighting.hlsl")
file(GLOB VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLighting/*.dxbc")
file(GLOB VLS_COMPOSITE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedVLSCompositeLinearLighting/*.dxbc")
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
set(IBL_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblRuntime.cpp")
set(VANILLA_IBL_RUNTIME_SOURCE "${IBL_RUNTIME_SOURCE}")
set(IBL_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/ibl/IblRuntime.h")
set(IBL_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/ibl/IblSettings.h")
set(IBL_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblSettingsStore.cpp")
set(IBL_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblSettingsStore.h")
set(IBL_PROVIDER_MODEL
  "${SOURCE_DIR}/Features/ibl/IblProviderModel.h")
set(IBL_ENVIRONMENT_PROVIDER_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentProvider.cpp")
set(IBL_ENVIRONMENT_PROVIDER_HEADER
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentProvider.h")
set(IBL_ENVIRONMENT_UPDATER_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentUpdater.cpp")
set(IBL_ENVIRONMENT_UPDATER_HEADER
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentUpdater.h")
set(IBL_COMPUTE_STATE_SCOPE_SOURCE
  "${SOURCE_DIR}/render/ComputeStateScope.cpp")
set(IBL_COMPUTE_STATE_SCOPE_HEADER
  "${SOURCE_DIR}/render/ComputeStateScope.h")
set(GPU_TIMING_PROFILER_SOURCE
  "${SOURCE_DIR}/render/GpuTimingProfiler.cpp")
set(GPU_TIMING_PROFILER_HEADER
  "${SOURCE_DIR}/render/GpuTimingProfiler.h")
set(IBL_PROJECTION_MODEL
  "${SOURCE_DIR}/Features/ibl/IblProjectionModel.h")
set(IBL_CAPTURE_PROBE_MODEL
  "${SOURCE_DIR}/Features/ibl/IblCaptureProbeModel.h")
set(IBL_SCENE_RADIANCE_PROBE_MODEL
  "${SOURCE_DIR}/Features/ibl/IblSceneRadianceProbeModel.h")
set(IBL_REFLECTION_FREE_CAPTURE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblReflectionFreeCapture.cpp")
set(IBL_REFLECTION_FREE_CAPTURE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblReflectionFreeCapture.h")
set(IBL_CAPTURE_PROBE_VERIFIER
  "${ROOT_DIR}/tools/verify_ibl_capture_probe_contracts.py")
set(IBL_MATERIAL_BINDING_SCOPE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblMaterialBindingScope.cpp")
set(IBL_MATERIAL_BINDING_SCOPE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblMaterialBindingScope.h")
set(IBL_MATERIAL_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_ibl_material_contracts.py")
set(IBL_MATERIAL_DXBC_TRANSFORM
  "${ROOT_DIR}/tools/dxbc_transform.py")
set(IBL_MATERIAL_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/ibl/GeneratedIblMaterialContracts.inl")
set(CONTACT_SHADOW_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowRuntime.cpp")
set(CONTACT_SHADOW_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowRuntime.h")
set(CONTACT_SHADOW_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettings.h")
set(CONTACT_SHADOW_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettingsStore.cpp")
set(CONTACT_SHADOW_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettingsStore.h")
set(CONTACT_SHADOW_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowTransform.hlsl")
set(CONTACT_SHADOW_WRAPPED_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/WrappedGrassTransform.hlsl")
set(CONTACT_SHADOW_HAIR_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/HairSpecularTransform.hlsl")
set(CONTACT_SHADOW_WETNESS_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/BasicWetnessTransform.hlsl")
set(CONTACT_SHADOW_MASK_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowMaskCS.hlsl")
set(CONTACT_SHADOW_DISPATCH_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowDispatchCS.hlsl")
set(CONTACT_SHADOW_RESOLVE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowResolveCS.hlsl")
set_source_files_properties(
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
  "${CONTACT_SHADOW_HAIR_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(CONTACT_SHADOW_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_contact_shadow_shader.py")
set(CONTACT_SHADOW_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyContactShadows.cmake")
set(SKYLIGHTING_SOURCE_DIR
  "${SOURCE_DIR}/Features/skylighting")
set(SKYLIGHTING_RUNTIME_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingRuntime.cpp")
set(SKYLIGHTING_RUNTIME_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingRuntime.h")
set(SKYLIGHTING_NATIVE_HOOK_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingNativeHooks.cpp")
set(SKYLIGHTING_NATIVE_HOOK_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingNativeHooks.h")
set(SKYLIGHTING_SETTINGS_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettings.h")
set(SKYLIGHTING_SETTINGS_STORE_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettingsStore.cpp")
set(SKYLIGHTING_SETTINGS_STORE_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettingsStore.h")
set(SKYLIGHTING_AMBIENT_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/SkylightingAmbientTransform.hlsl")
set(SKYLIGHTING_COMPUTE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/UpdateProbesCS.hlsl")
set(SKYLIGHTING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_skylighting_shaders.py")
set(SKYLIGHTING_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifySkylighting.cmake")
set_source_files_properties(
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(FILMIC_TONEMAPPING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingRuntime.cpp")
set(FILMIC_TONEMAPPING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingRuntime.h")
set(FILMIC_TONEMAPPING_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettings.h")
set(FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettingsStore.cpp")
set(FILMIC_TONEMAPPING_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h")
set(FILMIC_TONEMAPPING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/FilmicTonemapping/FilmicTonemappingPS.hlsl")
set(FILMIC_TONEMAPPING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_filmic_tonemapping_shader.py")
set(FILMIC_TONEMAPPING_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyFilmicTonemapping.cmake")
set_source_files_properties(
  "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(SURFACE_CLASSIFICATION_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/surface_classification/SurfaceClassificationRuntime.cpp")
set(SURFACE_CLASSIFICATION_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/surface_classification/SurfaceClassificationRuntime.h")
set(SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
set(SURFACE_CLASSIFICATION_GENERATOR
  "${ROOT_DIR}/tools/generate_surface_classification_shaders.py")
set(SURFACE_CLASSIFICATION_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/surface_classification/GeneratedSurfaceClassContracts.inl")
set(SURFACE_CLASSIFICATION_RUNTIME_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifySurfaceClassificationRuntime.cmake")
set(WRAPPED_GRASS_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassRuntime.cpp")
set(WRAPPED_GRASS_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassRuntime.h")
set(WRAPPED_GRASS_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettings.h")
set(WRAPPED_GRASS_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettingsStore.cpp")
set(WRAPPED_GRASS_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettingsStore.h")
set(HAIR_SPECULAR_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularRuntime.cpp")
set(HAIR_SPECULAR_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularRuntime.h")
set(HAIR_SPECULAR_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettings.h")
set(HAIR_SPECULAR_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettingsStore.cpp")
set(HAIR_SPECULAR_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettingsStore.h")
set(BASIC_WETNESS_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessRuntime.cpp")
set(BASIC_WETNESS_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessRuntime.h")
set(BASIC_WETNESS_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettings.h")
set(BASIC_WETNESS_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettingsStore.cpp")
set(BASIC_WETNESS_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettingsStore.h")
set(CLOUD_SHADOW_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowRuntime.cpp")
set(CLOUD_SHADOW_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowRuntime.h")
set(CLOUD_SHADOW_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettings.h")
set(CLOUD_SHADOW_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettingsStore.cpp")
set(CLOUD_SHADOW_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettingsStore.h")
set(SUBSURFACE_SCATTERING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringRuntime.cpp")
set(SUBSURFACE_SCATTERING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringRuntime.h")
set(SUBSURFACE_SCATTERING_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettings.h")
set(SUBSURFACE_SCATTERING_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.cpp")
set(SUBSURFACE_SCATTERING_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h")
set(SUBSURFACE_SCATTERING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/SubsurfaceScattering/SubsurfaceScatteringCS.hlsl")
set(SUBSURFACE_SCATTERING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_subsurface_scattering_shader.py")
set_source_files_properties(
  "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(COMPLEX_MATERIAL_PRODUCER_MODEL
  "${SOURCE_DIR}/Features/complex_materials/ComplexMaterialProducerModel.h")
set(COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_complex_material_producer_contracts.py")
set(COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/complex_materials/GeneratedComplexMaterialProducerContracts.inl")
set(COMPLEX_ENVIRONMENT_MODEL
  "${SOURCE_DIR}/Features/complex_materials/ComplexEnvironmentMaterialModel.h")
set(COMPLEX_ENVIRONMENT_GENERATOR
  "${ROOT_DIR}/tools/generate_complex_environment_contracts.py")
set(COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/complex_materials/GeneratedComplexEnvironmentContracts.inl")
set(COMPLEX_ENVIRONMENT_HLSL
  "${ROOT_DIR}/package/Shaders/Community/ComplexMaterials/ComplexEnvironmentMaterials.hlsli")
set_source_files_properties(
  "${COMPLEX_ENVIRONMENT_HLSL}" PROPERTIES HEADER_FILE_ONLY TRUE)
set(COMPLEX_PARALLAX_HLSL_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Reconstruction/LandscapeFourLayerSixMrt_L3_00000023.LinearLightingCandidate.hlsl")
set(COMPLEX_PARALLAX_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettings.h")
set(COMPLEX_PARALLAX_MODEL_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxModel.h")
set(COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettingsStore.cpp")
set(COMPLEX_PARALLAX_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettingsStore.h")
set(COMPLEX_PARALLAX_TEST_SOURCE
  "${ROOT_DIR}/tests/ComplexParallaxModelTests.cpp")
set(COMPLEX_PARALLAX_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyComplexParallax.cmake")
set(IBL_MATERIAL_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/IblMaterialBlendTemplate.hlsl")
set_source_files_properties(
  "${IBL_MATERIAL_SHADER_SOURCE}" PROPERTIES HEADER_FILE_ONLY TRUE)
file(GLOB IBL_MATERIAL_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/IBLMaterial/*.dxbc")
file(GLOB IBL_MATERIAL_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedIBLMaterial/*.dxbc")
set(IBL_PROJECTION_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/DiffuseIblProjectionCS.hlsl")
set(IBL_PROJECTION_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/DiffuseIblProjectionCS.dxbc")
set(IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/UpdateEnvironmentCS.hlsl")
set(IBL_ENVIRONMENT_UPDATE_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/UpdateEnvironmentCS.dxbc")
set(IBL_ENVIRONMENT_FILTER_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/FilterEnvironmentCS.hlsl")
set(IBL_ENVIRONMENT_FILTER_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/FilterEnvironmentCS.dxbc")
set(IBL_PROJECTION_SHADER_VERIFIER
  "${ROOT_DIR}/tools/verify_ibl_projection_shader.py")
set(GEOMETRY_HOOK_SOURCE "${RENDERER_HOOK_SOURCE}")
set(VANILLA_DEVMENU_MANIFEST_SOURCE "${DEVMENU_MANIFEST_SOURCE}")
set(VANILLA_SHARED_SETTINGS_SOURCE "${SHARED_SETTINGS_RUNTIME_SOURCE}")
include("${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake")
include("${DLAA_VERIFY_SCRIPT}")
include("${VANILLA_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyIblFoundation.cmake")
include("${CONTACT_SHADOW_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake")
include("${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake")
include("${DEVMENU_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake")
include("${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake")
include("${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  "${RENDERER_HOOK_SOURCE}"
  "${DFPREPASS_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyBSDFPrePassHook.cmake"
  "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  "${ROOT_DIR}/cmake/TestVerifyFo4VrRendererHookNegative.cmake"
  "${SETTINGS_PATH_SOURCE}"
  "${SETTINGS_PATH_HEADER}"
  "${SHARED_SETTINGS_RUNTIME_SOURCE}"
  "${SHARED_SETTINGS_RUNTIME_HEADER}"
  "${DEVMENU_VERIFY_SCRIPT}"
  "${D3D11_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  "${DLAA_RUNTIME_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaRuntime.h"
  "${DLAA_SOURCE_DIR}/DlaaSettings.h"
  "${DLAA_SETTINGS_STORE_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaSettingsStore.h"
  "${DLAA_SOURCE_DIR}/Fo4VrFrameBuffer.h"
  "${DLAA_ENGINE_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaEngineHooks.h"
  "${DLAA_D3D11_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.h"
  "${DLAA_STREAMLINE_SOURCE}"
  "${DLAA_SOURCE_DIR}/StreamlineBackend.h"
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  "${DLAA_VERIFY_SCRIPT}"
  "${ROOT_DIR}/cmake/TestVerifyDlaaNegative.cmake"
  "${ROOT_DIR}/cmake/StreamlinePayloadHashes.in.h"
  "${ROOT_DIR}/cmake/VerifyStreamlinePayload.ps1"
  "${VANILLA_VERIFY_SCRIPT}"
  "${VANILLA_SHADER_RUNTIME_SOURCE}"
  "${VANILLA_FOCUS_RUNTIME_SOURCE}"
  "${VANILLA_RUNTIME_SOURCE}"
  "${VANILLA_SETTINGS_STORE_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_SOURCE}"
  "${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
  "${VANILLA_IBL_GENERATOR_SOURCE}"
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  "${IBL_RUNTIME_SOURCE}"
  "${IBL_RUNTIME_HEADER}"
  "${IBL_SETTINGS_HEADER}"
  "${IBL_SETTINGS_STORE_SOURCE}"
  "${IBL_SETTINGS_STORE_HEADER}"
  "${IBL_PROVIDER_MODEL}"
  "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
  "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
  "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATER_HEADER}"
  "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
  "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
  "${GPU_TIMING_PROFILER_SOURCE}"
  "${GPU_TIMING_PROFILER_HEADER}"
  "${IBL_PROJECTION_MODEL}"
  "${IBL_CAPTURE_PROBE_MODEL}"
  "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
  "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
  "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
  "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
  "${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
  "${IBL_MATERIAL_CONTRACT_GENERATOR}"
  "${IBL_MATERIAL_DXBC_TRANSFORM}"
  "${IBL_MATERIAL_GENERATED_CONTRACTS}"
  "${SKYLIGHTING_RUNTIME_SOURCE}"
  "${SKYLIGHTING_RUNTIME_HEADER}"
  "${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
  "${SKYLIGHTING_NATIVE_HOOK_HEADER}"
  "${SKYLIGHTING_SETTINGS_HEADER}"
  "${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
  "${SKYLIGHTING_SETTINGS_STORE_HEADER}"
  "${SKYLIGHTING_SHADER_GENERATOR}"
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  "${SKYLIGHTING_AMBIENT_HEADER}"
  "${SKYLIGHTING_COMPUTE_HEADER}"
  "${CONTACT_SHADOW_RUNTIME_SOURCE}"
  "${CONTACT_SHADOW_RUNTIME_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_SHADER_GENERATOR}"
  "${CONTACT_SHADOW_VERIFY_SCRIPT}"
  "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
  "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
  "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_MODEL}"
  "${COMPLEX_ENVIRONMENT_GENERATOR}"
  "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_HLSL}"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_HEADER}"
  "${IBL_MATERIAL_SHADER_SOURCE}"
  ${IBL_MATERIAL_SHADER_ASSETS}
  ${IBL_MATERIAL_VERIFIED_ASSETS}
  "${IBL_CAPTURE_PROBE_VERIFIER}"
  "${IBL_PROJECTION_SHADER_SOURCE}"
  "${IBL_PROJECTION_SHADER_ASSET}"
  "${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}"
  "${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}"
  "${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}"
  "${IBL_PROJECTION_SHADER_VERIFIER}"
  "${ROOT_DIR}/cmake/VerifyIblFoundation.cmake"
  "${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${LINEAR_LIGHTING_RUNTIME_HEADER}"
  "${POINT_LIGHT_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake"
  "${DEVMENU_VERIFY_SCRIPT}"
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
  "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${VLS_COMPOSITE_LINEAR_LIGHTING_VERIFIED_ASSETS}
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
  "${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake"
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

add_custom_target(VerifyBSDFPrePassHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DDFPREPASS_HOOK_SOURCE=${DFPREPASS_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyBSDFPrePassHook.cmake"
  COMMENT "Verifying FO4VR BSDFPrePass descriptor hook contract"
  VERBATIM)

add_custom_target(VerifyDevMenu ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    "-DPROJECT_CMAKE_SOURCE=${ROOT_DIR}/CMakeLists.txt"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DSOURCE_ROOT=${ROOT_DIR}"
    -P "${DEVMENU_VERIFY_SCRIPT}"
  COMMENT "Verifying DevMenu-only runtime and package ownership"
  VERBATIM)

add_custom_target(VerifyD3D11Hooks ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  COMMENT "Verifying D3D11 method-detour ownership contract"
  VERBATIM)

add_custom_target(VerifyDlaa ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DDLAA_ENGINE_SOURCE=${DLAA_ENGINE_HOOK_SOURCE}"
    "-DDLAA_D3D_SOURCE=${DLAA_D3D11_HOOK_SOURCE}"
    "-DDLAA_RUNTIME_SOURCE=${DLAA_RUNTIME_SOURCE}"
    "-DDLAA_FRAME_HEADER=${DLAA_SOURCE_DIR}/Fo4VrFrameBuffer.h"
    "-DDLAA_STREAMLINE_SOURCE=${DLAA_STREAMLINE_SOURCE}"
    "-DDLAA_SETTINGS_SOURCE=${DLAA_SETTINGS_STORE_SOURCE}"
    "-DUPSCALING_COMPOSE_SHADER_SOURCE=${UPSCALING_COMPOSE_SHADER_SOURCE}"
    "-DUPSCALING_MOTION_SHADER_SOURCE=${UPSCALING_MOTION_SHADER_SOURCE}"
    "-DUPSCALING_REACTIVE_SHADER_SOURCE=${UPSCALING_REACTIVE_SHADER_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRENDERER_D3D_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${DLAA_VERIFY_SCRIPT}"
  COMMAND "${CMAKE_COMMAND}"
    "-DDLAA_VALIDATOR=${DLAA_VERIFY_SCRIPT}"
    "-DDLAA_ENGINE_SOURCE=${DLAA_ENGINE_HOOK_SOURCE}"
    "-DDLAA_D3D_SOURCE=${DLAA_D3D11_HOOK_SOURCE}"
    "-DDLAA_RUNTIME_SOURCE=${DLAA_RUNTIME_SOURCE}"
    "-DDLAA_FRAME_HEADER=${DLAA_SOURCE_DIR}/Fo4VrFrameBuffer.h"
    "-DDLAA_STREAMLINE_SOURCE=${DLAA_STREAMLINE_SOURCE}"
    "-DDLAA_SETTINGS_SOURCE=${DLAA_SETTINGS_STORE_SOURCE}"
    "-DUPSCALING_COMPOSE_SHADER_SOURCE=${UPSCALING_COMPOSE_SHADER_SOURCE}"
    "-DUPSCALING_MOTION_SHADER_SOURCE=${UPSCALING_MOTION_SHADER_SOURCE}"
    "-DUPSCALING_REACTIVE_SHADER_SOURCE=${UPSCALING_REACTIVE_SHADER_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRENDERER_D3D_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/TestVerifyDlaaNegative.cmake"
  COMMENT "Verifying FO4VR-native DLAA ownership and negative fixtures"
  VERBATIM)

add_custom_target(VerifyStreamlinePayload ALL
  COMMAND "${POWERSHELL_EXECUTABLE}"
    -NoProfile
    -NonInteractive
    -ExecutionPolicy Bypass
    -File "${ROOT_DIR}/cmake/VerifyStreamlinePayload.ps1"
    -RuntimeDirectory "${STREAMLINE_RUNTIME_DIR}"
  COMMENT "Verifying NVIDIA signatures and hashes for Streamline DLAA payload"
  VERBATIM)

add_executable(VanillaReflectionPatchTool
  "${VANILLA_REFLECTION_PATCH_TOOL_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h")
target_compile_features(VanillaReflectionPatchTool PRIVATE cxx_std_23)
target_include_directories(VanillaReflectionPatchTool PRIVATE "${SOURCE_DIR}")
target_compile_options(VanillaReflectionPatchTool PRIVATE
  /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)

add_custom_target(VerifyIblFoundation ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DIBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
    "-DIBL_RUNTIME_HEADER=${IBL_RUNTIME_HEADER}"
    "-DIBL_SETTINGS_HEADER=${IBL_SETTINGS_HEADER}"
    "-DIBL_SETTINGS_STORE_SOURCE=${IBL_SETTINGS_STORE_SOURCE}"
    "-DIBL_SETTINGS_STORE_HEADER=${IBL_SETTINGS_STORE_HEADER}"
    "-DIBL_PROVIDER_MODEL=${IBL_PROVIDER_MODEL}"
    "-DIBL_ENVIRONMENT_PROVIDER_SOURCE=${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "-DIBL_ENVIRONMENT_PROVIDER_HEADER=${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "-DIBL_ENVIRONMENT_UPDATER_SOURCE=${IBL_ENVIRONMENT_UPDATER_SOURCE}"
    "-DIBL_ENVIRONMENT_UPDATER_HEADER=${IBL_ENVIRONMENT_UPDATER_HEADER}"
    "-DIBL_COMPUTE_STATE_SCOPE_SOURCE=${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "-DIBL_COMPUTE_STATE_SCOPE_HEADER=${IBL_COMPUTE_STATE_SCOPE_HEADER}"
    "-DIBL_PROJECTION_MODEL=${IBL_PROJECTION_MODEL}"
    "-DIBL_CAPTURE_PROBE_MODEL=${IBL_CAPTURE_PROBE_MODEL}"
    "-DIBL_SCENE_RADIANCE_PROBE_MODEL=${IBL_SCENE_RADIANCE_PROBE_MODEL}"
    "-DIBL_REFLECTION_FREE_CAPTURE_SOURCE=${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "-DIBL_REFLECTION_FREE_CAPTURE_HEADER=${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
    "-DIBL_MATERIAL_BINDING_SCOPE_SOURCE=${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
    "-DIBL_MATERIAL_BINDING_SCOPE_HEADER=${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
    "-DIBL_MATERIAL_GENERATED_CONTRACTS=${IBL_MATERIAL_GENERATED_CONTRACTS}"
    "-DIBL_PROJECTION_SHADER_SOURCE=${IBL_PROJECTION_SHADER_SOURCE}"
    "-DIBL_PROJECTION_SHADER_ASSET=${IBL_PROJECTION_SHADER_ASSET}"
    "-DIBL_ENVIRONMENT_UPDATE_SHADER_SOURCE=${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}"
    "-DIBL_ENVIRONMENT_UPDATE_SHADER_ASSET=${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}"
    "-DIBL_ENVIRONMENT_FILTER_SHADER_SOURCE=${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}"
    "-DIBL_ENVIRONMENT_FILTER_SHADER_ASSET=${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DGEOMETRY_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyIblFoundation.cmake"
  COMMENT "Verifying FO4VR-native IBL provider and diffuse foundation"
  VERBATIM)

add_custom_target(VerifyIblProjectionShaderAsset ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_PROJECTION_SHADER_VERIFIER}"
    --root "${ROOT_DIR}"
    --check
  COMMENT "Verifying generated IBL projection shader identity"
  VERBATIM)

add_custom_target(VerifyIblCaptureProbeContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_CAPTURE_PROBE_VERIFIER}"
    --root "${ROOT_DIR}"
  COMMENT "Verifying exact DFComposite IBL capture contracts"
  VERBATIM)

add_custom_target(VerifyIblMaterialContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_MATERIAL_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${IBL_MATERIAL_GENERATED_CONTRACTS}"
    --surface-anchor-tool "$<TARGET_FILE:VanillaReflectionPatchTool>"
    --check
  DEPENDS VanillaReflectionPatchTool
  COMMENT "Verifying exact DFComposite IBL material contracts"
  VERBATIM)

add_custom_target(VerifyComplexMaterialProducerContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying exact FO4VR complex-material producer contracts"
  VERBATIM)

add_custom_target(VerifyComplexEnvironmentContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${COMPLEX_ENVIRONMENT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying descriptor-gated complex environment contracts"
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
    "-DCLOUD_SHADOW_RUNTIME_SOURCE=${CLOUD_SHADOW_RUNTIME_SOURCE}"
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

add_custom_target(VerifyVLSCompositeLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DVLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE=${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake"
  COMMENT "Verifying frame-only VLS composite Linear Lighting ownership"
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

add_custom_target(VerifyVLSCompositeLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
    --check
  COMMENT "Verifying FO4VR VLS composite Linear Lighting contract"
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

find_program(FO4VR_CS_FXC_EXECUTABLE
  NAMES fxc.exe fxc
  HINTS
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
  REQUIRED)
set(VANILLA_FIXES_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/VanillaFixes")
set(VANILLA_SAO_BLUR_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoBlurHCS.dxbc")
set(VANILLA_SAO_BLUR_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoBlurHCS.h")
set(VANILLA_SAO_RAW_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoRawAOCS.dxbc")
set(VANILLA_SAO_RAW_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoRawAOCS.h")
set(VANILLA_SSLR_BLUR_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrBlurHVS.dxbc")
set(VANILLA_SSLR_BLUR_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrBlurHVS.h")
set(VANILLA_SSLR_PREPASS_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrPrepassPS.dxbc")
set(VANILLA_SSLR_PREPASS_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrPrepassPS.h")
set(VANILLA_SSLR_RAYTRACE_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrRaytracePS.dxbc")
set(VANILLA_SSLR_RAYTRACE_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrRaytracePS.h")
add_custom_command(
  OUTPUT
    "${VANILLA_SAO_BLUR_BINARY}" "${VANILLA_SAO_BLUR_HEADER}"
    "${VANILLA_SAO_RAW_BINARY}" "${VANILLA_SAO_RAW_HEADER}"
    "${VANILLA_SSLR_BLUR_BINARY}" "${VANILLA_SSLR_BLUR_HEADER}"
    "${VANILLA_SSLR_PREPASS_BINARY}" "${VANILLA_SSLR_PREPASS_HEADER}"
    "${VANILLA_SSLR_RAYTRACE_BINARY}" "${VANILLA_SSLR_RAYTRACE_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${VANILLA_FIXES_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SAO_BLUR_BINARY}"
    "/Fh${VANILLA_SAO_BLUR_HEADER}"
    /Vnfo4vr_cs_vanilla_sao_blur_h_cs
    "${VANILLA_SAO_BLUR_SOURCE}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SAO_RAW_BINARY}"
    "/Fh${VANILLA_SAO_RAW_HEADER}"
    /Vnfo4vr_cs_vanilla_sao_raw_ao_cs
    "${VANILLA_SAO_RAW_SOURCE}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T vs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SSLR_BLUR_BINARY}"
    "/Fh${VANILLA_SSLR_BLUR_HEADER}"
    /Vnfo4vr_cs_vanilla_sslr_blur_h_vs
    "${VANILLA_SSLR_BLUR_SOURCE}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SSLR_PREPASS_BINARY}"
    "/Fh${VANILLA_SSLR_PREPASS_HEADER}"
    /Vnfo4vr_cs_vanilla_sslr_prepass_ps
    "${VANILLA_SSLR_PREPASS_SOURCE}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/Fo${VANILLA_SSLR_RAYTRACE_BINARY}"
    "/Fh${VANILLA_SSLR_RAYTRACE_HEADER}"
    /Vnfo4vr_cs_vanilla_sslr_raytrace_ps
    "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  DEPENDS
    "${VANILLA_SAO_BLUR_SOURCE}"
    "${VANILLA_SAO_RAW_SOURCE}"
    "${VANILLA_SSLR_BLUR_SOURCE}"
    "${VANILLA_SSLR_PREPASS_SOURCE}"
    "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  COMMENT "Compiling stereo-safe Vanilla Fixes shaders"
  VERBATIM)
add_custom_target(VanillaFixesShaders
  DEPENDS
    "${VANILLA_SAO_BLUR_BINARY}" "${VANILLA_SAO_BLUR_HEADER}"
    "${VANILLA_SAO_RAW_BINARY}" "${VANILLA_SAO_RAW_HEADER}"
    "${VANILLA_SSLR_BLUR_BINARY}" "${VANILLA_SSLR_BLUR_HEADER}"
    "${VANILLA_SSLR_PREPASS_BINARY}" "${VANILLA_SSLR_PREPASS_HEADER}"
    "${VANILLA_SSLR_RAYTRACE_BINARY}" "${VANILLA_SSLR_RAYTRACE_HEADER}")
add_custom_command(
  OUTPUT
    "${UPSCALING_COMPOSE_SHADER_BINARY}"
    "${UPSCALING_COMPOSE_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${UPSCALING_COMPOSE_SHADER_BINARY}"
    "/Fh${UPSCALING_COMPOSE_SHADER_HEADER}"
    /Vnfo4vr_cs_upscaling_compose_cs
    "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  COMMENT "Compiling stereo upscaling compositor"
  VERBATIM)
add_custom_command(
  OUTPUT
    "${UPSCALING_MOTION_SHADER_BINARY}"
    "${UPSCALING_MOTION_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${UPSCALING_MOTION_SHADER_BINARY}"
    "/Fh${UPSCALING_MOTION_SHADER_HEADER}"
    /Vnfo4vr_cs_upscaling_motion_repair_cs
    "${UPSCALING_MOTION_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_MOTION_SHADER_SOURCE}"
  COMMENT "Compiling per-eye FO4VR motion-vector repair"
  VERBATIM)
add_custom_command(
  OUTPUT
    "${UPSCALING_REACTIVE_SHADER_BINARY}"
    "${UPSCALING_REACTIVE_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${UPSCALING_REACTIVE_SHADER_BINARY}"
    "/Fh${UPSCALING_REACTIVE_SHADER_HEADER}"
    /Vnfo4vr_cs_upscaling_reactive_mask_cs
    "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  COMMENT "Compiling FO4VR TAA-derived DLSS reactive mask"
  VERBATIM)
add_custom_target(UpscalingShaders
  DEPENDS
    "${UPSCALING_COMPOSE_SHADER_BINARY}"
    "${UPSCALING_COMPOSE_SHADER_HEADER}"
    "${UPSCALING_MOTION_SHADER_BINARY}"
    "${UPSCALING_MOTION_SHADER_HEADER}"
    "${UPSCALING_REACTIVE_SHADER_BINARY}"
    "${UPSCALING_REACTIVE_SHADER_HEADER}")
add_custom_target(VerifyVanillaFixes ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DVANILLA_SHADER_RUNTIME_SOURCE=${VANILLA_SHADER_RUNTIME_SOURCE}"
    "-DVANILLA_FOCUS_RUNTIME_SOURCE=${VANILLA_FOCUS_RUNTIME_SOURCE}"
    "-DVANILLA_RUNTIME_SOURCE=${VANILLA_RUNTIME_SOURCE}"
    "-DVANILLA_SETTINGS_STORE_SOURCE=${VANILLA_SETTINGS_STORE_SOURCE}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
    "-DVANILLA_REFLECTION_PATCH_SOURCE=${VANILLA_REFLECTION_PATCH_SOURCE}"
    "-DVANILLA_SSLR_ENVIRONMENT_SOURCE=${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
    "-DVANILLA_IBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
    "-DVANILLA_D3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DVANILLA_DEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    "-DVANILLA_SHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DVANILLA_PLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DVANILLA_IBL_GENERATOR_SOURCE=${VANILLA_IBL_GENERATOR_SOURCE}"
    "-DVANILLA_SAO_BLUR_SOURCE=${VANILLA_SAO_BLUR_SOURCE}"
    "-DVANILLA_SAO_RAW_SOURCE=${VANILLA_SAO_RAW_SOURCE}"
    "-DVANILLA_SSLR_BLUR_SOURCE=${VANILLA_SSLR_BLUR_SOURCE}"
    "-DVANILLA_SSLR_PREPASS_SOURCE=${VANILLA_SSLR_PREPASS_SOURCE}"
    "-DVANILLA_SSLR_RAYTRACE_SOURCE=${VANILLA_SSLR_RAYTRACE_SOURCE}"
    "-DVANILLA_FXC_EXECUTABLE=${FO4VR_CS_FXC_EXECUTABLE}"
    "-DVANILLA_SAO_BLUR_BINARY=${VANILLA_SAO_BLUR_BINARY}"
    "-DVANILLA_SAO_RAW_BINARY=${VANILLA_SAO_RAW_BINARY}"
    "-DVANILLA_SSLR_BLUR_BINARY=${VANILLA_SSLR_BLUR_BINARY}"
    "-DVANILLA_SSLR_PREPASS_BINARY=${VANILLA_SSLR_PREPASS_BINARY}"
    "-DVANILLA_SSLR_RAYTRACE_BINARY=${VANILLA_SSLR_RAYTRACE_BINARY}"
    -P "${VANILLA_VERIFY_SCRIPT}"
  DEPENDS VanillaFixesShaders
  COMMENT "Verifying Vanilla Fixes native, shader, settings, and DevMenu contracts"
  VERBATIM)
add_custom_target(VerifyNativeShadows ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DNATIVE_SHADOW_RUNTIME_SOURCE=${NATIVE_SHADOW_RUNTIME_SOURCE}"
    "-DNATIVE_SHADOW_SETTINGS_HEADER=${NATIVE_SHADOW_SETTINGS_HEADER}"
    "-DNATIVE_SHADOW_SETTINGS_STORE_SOURCE=${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
    "-DNATIVE_SHADOW_PATCH_MODEL=${NATIVE_SHADOW_PATCH_MODEL}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    -P "${NATIVE_SHADOW_VERIFY_SCRIPT}"
  COMMENT "Verifying fixed native shadow contracts"
  VERBATIM)
add_custom_target(VerifySurfaceClassification ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${SURFACE_CLASSIFICATION_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${FO4VR_CS_FXC_EXECUTABLE}"
    --check
  COMMAND "${CMAKE_COMMAND}"
    -DSURFACE_CLASSIFICATION_RUNTIME_SOURCE=${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}
    -DSURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE=${SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE}
    -DSURFACE_CLASSIFICATION_GENERATOR=${SURFACE_CLASSIFICATION_GENERATOR}
    -P "${SURFACE_CLASSIFICATION_RUNTIME_VERIFY_SCRIPT}"
  COMMENT "Verifying exact material surface-classification shaders"
  VERBATIM)
set(CONTACT_SHADOW_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ContactShadows")
set(CONTACT_SHADOW_SHADER_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowsDFLight.dxbc")
set(CONTACT_SHADOW_SHADER_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowsDFLight.h")
set(CONTACT_SHADOW_MASK_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowMaskCS.dxbc")
set(CONTACT_SHADOW_MASK_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowMaskCS.h")
set(CONTACT_SHADOW_DISPATCH_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowDispatchCS.dxbc")
set(CONTACT_SHADOW_DISPATCH_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowDispatchCS.h")
set(CONTACT_SHADOW_RESOLVE_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowResolveCS.dxbc")
set(CONTACT_SHADOW_RESOLVE_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowResolveCS.h")
add_custom_command(
  OUTPUT
    "${CONTACT_SHADOW_SHADER_BINARY}"
    "${CONTACT_SHADOW_SHADER_HEADER}"
    "${CONTACT_SHADOW_MASK_BINARY}"
    "${CONTACT_SHADOW_MASK_HEADER}"
    "${CONTACT_SHADOW_DISPATCH_BINARY}"
    "${CONTACT_SHADOW_DISPATCH_HEADER}"
    "${CONTACT_SHADOW_RESOLVE_BINARY}"
    "${CONTACT_SHADOW_RESOLVE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${CONTACT_SHADOW_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${FO4VR_CS_FXC_EXECUTABLE}"
    --binary "${CONTACT_SHADOW_SHADER_BINARY}"
    --header "${CONTACT_SHADOW_SHADER_HEADER}"
    --compute-binary "${CONTACT_SHADOW_MASK_BINARY}"
    --compute-header "${CONTACT_SHADOW_MASK_HEADER}"
    --dispatch-binary "${CONTACT_SHADOW_DISPATCH_BINARY}"
    --dispatch-header "${CONTACT_SHADOW_DISPATCH_HEADER}"
    --resolve-binary "${CONTACT_SHADOW_RESOLVE_BINARY}"
    --resolve-header "${CONTACT_SHADOW_RESOLVE_HEADER}"
  DEPENDS
    "${CONTACT_SHADOW_SHADER_GENERATOR}"
    "${CONTACT_SHADOW_SHADER_SOURCE}"
    "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
    "${CONTACT_SHADOW_HAIR_SHADER_SOURCE}"
    "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
    "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
    "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
    "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
    "${ROOT_DIR}/tools/dxbc_transform.py"
    "${ROOT_DIR}/Shaders012_VR.fxp"
  COMMENT "Generating exact stereo Contact Shadows DFLight shader"
  VERBATIM)
add_custom_target(ContactShadowShader
  DEPENDS
    "${CONTACT_SHADOW_SHADER_BINARY}"
    "${CONTACT_SHADOW_SHADER_HEADER}"
    "${CONTACT_SHADOW_MASK_BINARY}"
    "${CONTACT_SHADOW_MASK_HEADER}"
    "${CONTACT_SHADOW_DISPATCH_BINARY}"
    "${CONTACT_SHADOW_DISPATCH_HEADER}"
    "${CONTACT_SHADOW_RESOLVE_BINARY}"
    "${CONTACT_SHADOW_RESOLVE_HEADER}")
set(SKYLIGHTING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/Skylighting")
set(SKYLIGHTING_AMBIENT_HEADER
  "${SKYLIGHTING_GENERATED_DIR}/SkylightingAmbient.h")
set(SKYLIGHTING_COMPUTE_BINARY
  "${SKYLIGHTING_GENERATED_DIR}/UpdateProbesCS.dxbc")
set(SKYLIGHTING_COMPUTE_HEADER
  "${SKYLIGHTING_GENERATED_DIR}/UpdateProbesCS.h")
add_custom_command(
  OUTPUT
    "${SKYLIGHTING_AMBIENT_HEADER}"
    "${SKYLIGHTING_COMPUTE_BINARY}"
    "${SKYLIGHTING_COMPUTE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${SKYLIGHTING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${FO4VR_CS_FXC_EXECUTABLE}"
    --ambient-header "${SKYLIGHTING_AMBIENT_HEADER}"
    --compute-binary "${SKYLIGHTING_COMPUTE_BINARY}"
    --compute-header "${SKYLIGHTING_COMPUTE_HEADER}"
  DEPENDS
    "${SKYLIGHTING_SHADER_GENERATOR}"
    "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
    "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/dxbc_transform.py"
    "${DFLIGHT_AMBIENT_CONTRACT_MANIFEST}"
    ${DFLIGHT_AMBIENT_SHADER_ASSETS}
  COMMENT "Generating stereo world-space Skylighting shaders"
  VERBATIM)
add_custom_target(SkylightingShaders
  DEPENDS
    "${SKYLIGHTING_AMBIENT_HEADER}"
    "${SKYLIGHTING_COMPUTE_BINARY}"
    "${SKYLIGHTING_COMPUTE_HEADER}")
add_custom_target(VerifySkylighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DSKYLIGHTING_RUNTIME_SOURCE=${SKYLIGHTING_RUNTIME_SOURCE}"
    "-DSKYLIGHTING_RUNTIME_HEADER=${SKYLIGHTING_RUNTIME_HEADER}"
    "-DSKYLIGHTING_NATIVE_HOOK_SOURCE=${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
    "-DSKYLIGHTING_SETTINGS_HEADER=${SKYLIGHTING_SETTINGS_HEADER}"
    "-DSKYLIGHTING_SETTINGS_STORE_SOURCE=${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
    "-DSKYLIGHTING_AMBIENT_SHADER_SOURCE=${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
    "-DSKYLIGHTING_COMPUTE_SHADER_SOURCE=${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
    "-DSKYLIGHTING_SHADER_GENERATOR=${SKYLIGHTING_SHADER_GENERATOR}"
    "-DSKYLIGHTING_AMBIENT_HEADER=${SKYLIGHTING_AMBIENT_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    -P "${SKYLIGHTING_VERIFY_SCRIPT}"
  DEPENDS SkylightingShaders
  COMMENT "Verifying FO4VR-native stereo Skylighting contracts"
  VERBATIM)
set(FILMIC_TONEMAPPING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/FilmicTonemapping")
set(FILMIC_TONEMAPPING_SHADER_BINARY
  "${FILMIC_TONEMAPPING_GENERATED_DIR}/FilmicTonemappingPS.dxbc")
set(FILMIC_TONEMAPPING_SHADER_HEADER
  "${FILMIC_TONEMAPPING_GENERATED_DIR}/FilmicTonemappingPS.h")
add_custom_command(
  OUTPUT
    "${FILMIC_TONEMAPPING_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${FO4VR_CS_FXC_EXECUTABLE}"
    --binary "${FILMIC_TONEMAPPING_SHADER_BINARY}"
    --header "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  DEPENDS
    "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
    "${ROOT_DIR}/Shaders012_VR.fxp"
  COMMENT "Compiling exact FO4VR filmic HDR blend replacement"
  VERBATIM)
add_custom_target(FilmicTonemappingShader
  DEPENDS
    "${FILMIC_TONEMAPPING_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_SHADER_HEADER}")
add_custom_target(VerifyFilmicTonemapping ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DFILMIC_RUNTIME_SOURCE=${FILMIC_TONEMAPPING_RUNTIME_SOURCE}"
    "-DFILMIC_RUNTIME_HEADER=${FILMIC_TONEMAPPING_RUNTIME_HEADER}"
    "-DFILMIC_SETTINGS_HEADER=${FILMIC_TONEMAPPING_SETTINGS_HEADER}"
    "-DFILMIC_SETTINGS_STORE_SOURCE=${FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE}"
    "-DFILMIC_SHADER_SOURCE=${FILMIC_TONEMAPPING_SHADER_SOURCE}"
    "-DFILMIC_SHADER_GENERATOR=${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    -P "${FILMIC_TONEMAPPING_VERIFY_SCRIPT}"
  COMMENT "Verifying filmic tonemapping output-stage ownership"
  VERBATIM)
set(SUBSURFACE_SCATTERING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/SubsurfaceScattering")
set(SUBSURFACE_SCATTERING_SHADER_BINARY
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}/SubsurfaceScatteringCS.dxbc")
set(SUBSURFACE_SCATTERING_SHADER_HEADER
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}/SubsurfaceScatteringCS.h")
add_custom_command(
  OUTPUT
    "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${SUBSURFACE_SCATTERING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${FO4VR_CS_FXC_EXECUTABLE}"
    --binary "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    --header "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  DEPENDS
    "${SUBSURFACE_SCATTERING_SHADER_GENERATOR}"
    "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  COMMENT "Compiling stereo-safe subsurface scattering compute pass"
  VERBATIM)
add_custom_target(SubsurfaceScatteringShader
  DEPENDS
    "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    "${SUBSURFACE_SCATTERING_SHADER_HEADER}")
add_custom_target(VerifyContactShadows ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DCONTACT_SHADOW_RUNTIME_SOURCE=${CONTACT_SHADOW_RUNTIME_SOURCE}"
    "-DCONTACT_SHADOW_RUNTIME_HEADER=${CONTACT_SHADOW_RUNTIME_HEADER}"
    "-DCONTACT_SHADOW_SETTINGS_HEADER=${CONTACT_SHADOW_SETTINGS_HEADER}"
    "-DCONTACT_SHADOW_SETTINGS_STORE_SOURCE=${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "-DCONTACT_SHADOW_SETTINGS_STORE_HEADER=${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "-DCONTACT_SHADOW_SHADER_SOURCE=${CONTACT_SHADOW_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_MASK_SHADER_SOURCE=${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_DISPATCH_SHADER_SOURCE=${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_RESOLVE_SHADER_SOURCE=${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_SHADER_GENERATOR=${CONTACT_SHADOW_SHADER_GENERATOR}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    -P "${CONTACT_SHADOW_VERIFY_SCRIPT}"
  COMMENT "Verifying stereo Contact Shadows ownership contract"
  VERBATIM)
set(COMPLEX_PARALLAX_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ComplexParallax")
set(COMPLEX_PARALLAX_BASE_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeBase.dxbc")
set(COMPLEX_PARALLAX_BASE_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeBase.h")
set(COMPLEX_PARALLAX_LOD_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeLod.dxbc")
set(COMPLEX_PARALLAX_LOD_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeLod.h")
set(COMPLEX_PARALLAX_INSTANCED_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeInstancedLod.dxbc")
set(COMPLEX_PARALLAX_INSTANCED_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeInstancedLod.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeBase.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeBase.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeLod.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeLod.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeInstancedLod.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeInstancedLod.h")

add_custom_command(
  OUTPUT "${COMPLEX_PARALLAX_BASE_BINARY}" "${COMPLEX_PARALLAX_BASE_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    "/Fo${COMPLEX_PARALLAX_BASE_BINARY}"
    "/Fh${COMPLEX_PARALLAX_BASE_HEADER}"
    /Vnfo4vr_cs_complex_parallax_landscape_base
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling combined base landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT "${COMPLEX_PARALLAX_LOD_BINARY}" "${COMPLEX_PARALLAX_LOD_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    "/Fo${COMPLEX_PARALLAX_LOD_BINARY}"
    "/Fh${COMPLEX_PARALLAX_LOD_HEADER}"
    /Vnfo4vr_cs_complex_parallax_landscape_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling combined LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${COMPLEX_PARALLAX_INSTANCED_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_INSTANCED_LANDSCAPE=1
    "/Fo${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "/Fh${COMPLEX_PARALLAX_INSTANCED_HEADER}"
    /Vnfo4vr_cs_complex_parallax_landscape_instanced_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling combined instanced LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
    /Vnfo4vr_cs_surface_class_complex_parallax_landscape_base
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling classified base landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
    /Vnfo4vr_cs_surface_class_complex_parallax_landscape_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling classified LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${FO4VR_CS_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_INSTANCED_LANDSCAPE=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}"
    /Vnfo4vr_cs_surface_class_complex_parallax_landscape_instanced_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
  COMMENT "Compiling classified instanced LOD landscape complex parallax"
  VERBATIM)

add_custom_target(ComplexParallaxShaders
  DEPENDS
    "${COMPLEX_PARALLAX_BASE_BINARY}"
    "${COMPLEX_PARALLAX_BASE_HEADER}"
    "${COMPLEX_PARALLAX_LOD_BINARY}"
    "${COMPLEX_PARALLAX_LOD_HEADER}"
    "${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${COMPLEX_PARALLAX_INSTANCED_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}")

add_custom_target(VerifyComplexParallax ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DFXC_EXECUTABLE=${FO4VR_CS_FXC_EXECUTABLE}"
    "-DHLSL_SOURCE=${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "-DLINEAR_LIGHTING_INCLUDE=${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "-DRUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
    "-DBASE_BINARY=${COMPLEX_PARALLAX_BASE_BINARY}"
    "-DLOD_BINARY=${COMPLEX_PARALLAX_LOD_BINARY}"
    "-DINSTANCED_BINARY=${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    -P "${COMPLEX_PARALLAX_VERIFY_SCRIPT}"
  DEPENDS ComplexParallaxShaders
  COMMENT "Verifying combined stereo landscape complex parallax"
  VERBATIM)

add_library(FO4VRCommunityShadersCore STATIC
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.h"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
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
  "${IBL_RUNTIME_SOURCE}"
  "${IBL_RUNTIME_HEADER}"
  "${IBL_SETTINGS_HEADER}"
  "${IBL_SETTINGS_STORE_SOURCE}"
  "${IBL_SETTINGS_STORE_HEADER}"
  "${IBL_PROVIDER_MODEL}"
  "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
  "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
  "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATER_HEADER}"
  "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
  "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
  "${GPU_TIMING_PROFILER_SOURCE}"
  "${GPU_TIMING_PROFILER_HEADER}"
  "${IBL_PROJECTION_MODEL}"
  "${IBL_CAPTURE_PROBE_MODEL}"
  "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
  "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
  "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
  "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
  "${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
  "${IBL_MATERIAL_CONTRACT_GENERATOR}"
  "${IBL_MATERIAL_DXBC_TRANSFORM}"
  "${IBL_MATERIAL_GENERATED_CONTRACTS}"
  "${SKYLIGHTING_RUNTIME_SOURCE}"
  "${SKYLIGHTING_RUNTIME_HEADER}"
  "${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
  "${SKYLIGHTING_NATIVE_HOOK_HEADER}"
  "${SKYLIGHTING_SETTINGS_HEADER}"
  "${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
  "${SKYLIGHTING_SETTINGS_STORE_HEADER}"
  "${SKYLIGHTING_SHADER_GENERATOR}"
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  "${SKYLIGHTING_AMBIENT_HEADER}"
  "${SKYLIGHTING_COMPUTE_HEADER}"
  "${CONTACT_SHADOW_RUNTIME_SOURCE}"
  "${CONTACT_SHADOW_RUNTIME_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_SHADER_GENERATOR}"
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
  "${CONTACT_SHADOW_SHADER_HEADER}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_HEADER}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_HEADER}"
  "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
  "${CONTACT_SHADOW_RESOLVE_HEADER}"
  "${FILMIC_TONEMAPPING_RUNTIME_SOURCE}"
  "${FILMIC_TONEMAPPING_RUNTIME_HEADER}"
  "${FILMIC_TONEMAPPING_SETTINGS_HEADER}"
  "${FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE}"
  "${FILMIC_TONEMAPPING_SETTINGS_STORE_HEADER}"
  "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
  "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
  "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  "${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}"
  "${SURFACE_CLASSIFICATION_RUNTIME_HEADER}"
  "${SURFACE_CLASSIFICATION_GENERATOR}"
  "${SURFACE_CLASSIFICATION_GENERATED_CONTRACTS}"
  "${SOURCE_DIR}/GeneratedSurfaceClassificationResources.h"
  "${WRAPPED_GRASS_RUNTIME_SOURCE}"
  "${WRAPPED_GRASS_RUNTIME_HEADER}"
  "${WRAPPED_GRASS_SETTINGS_HEADER}"
  "${WRAPPED_GRASS_SETTINGS_STORE_SOURCE}"
  "${WRAPPED_GRASS_SETTINGS_STORE_HEADER}"
  "${HAIR_SPECULAR_RUNTIME_SOURCE}"
  "${HAIR_SPECULAR_RUNTIME_HEADER}"
  "${HAIR_SPECULAR_SETTINGS_HEADER}"
  "${HAIR_SPECULAR_SETTINGS_STORE_SOURCE}"
  "${HAIR_SPECULAR_SETTINGS_STORE_HEADER}"
  "${BASIC_WETNESS_RUNTIME_SOURCE}"
  "${BASIC_WETNESS_RUNTIME_HEADER}"
  "${BASIC_WETNESS_SETTINGS_HEADER}"
  "${BASIC_WETNESS_SETTINGS_STORE_SOURCE}"
  "${BASIC_WETNESS_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
  "${CLOUD_SHADOW_RUNTIME_SOURCE}"
  "${CLOUD_SHADOW_RUNTIME_HEADER}"
  "${CLOUD_SHADOW_SETTINGS_HEADER}"
  "${CLOUD_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CLOUD_SHADOW_SETTINGS_STORE_HEADER}"
  "${SUBSURFACE_SCATTERING_RUNTIME_SOURCE}"
  "${SUBSURFACE_SCATTERING_RUNTIME_HEADER}"
  "${SUBSURFACE_SCATTERING_SETTINGS_HEADER}"
  "${SUBSURFACE_SCATTERING_SETTINGS_STORE_SOURCE}"
  "${SUBSURFACE_SCATTERING_SETTINGS_STORE_HEADER}"
  "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
  "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
  "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_MODEL}"
  "${COMPLEX_ENVIRONMENT_GENERATOR}"
  "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_HLSL}"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_HEADER}"
  "${IBL_MATERIAL_SHADER_SOURCE}"
  ${IBL_MATERIAL_SHADER_ASSETS}
  ${IBL_MATERIAL_VERIFIED_ASSETS}
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
  "${SETTINGS_PATH_SOURCE}"
  "${SETTINGS_PATH_HEADER}"
  "${SHARED_SETTINGS_RUNTIME_SOURCE}"
  "${SHARED_SETTINGS_RUNTIME_HEADER}"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.h"
  "${DFPREPASS_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/BSDFPrePassShaderHook.h"
  "${D3D11_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/D3D11Hooks.h"
  "${DLAA_RUNTIME_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaRuntime.h"
  "${DLAA_SOURCE_DIR}/DlaaSettings.h"
  "${DLAA_SETTINGS_STORE_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaSettingsStore.h"
  "${DLAA_SOURCE_DIR}/Fo4VrFrameBuffer.h"
  "${DLAA_ENGINE_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaEngineHooks.h"
  "${DLAA_D3D11_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.h"
  "${DLAA_STREAMLINE_SOURCE}"
  "${DLAA_SOURCE_DIR}/StreamlineBackend.h"
  "${DLAA_GENERATED_DIR}/GeneratedStreamlinePayloadHashes.h"
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_COMPOSE_SHADER_HEADER}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_HEADER}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_HEADER}"
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.h"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_HEADER}"
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.h"
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettings.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.h"
  "${NATIVE_SHADOW_RUNTIME_SOURCE}"
  "${NATIVE_SHADOW_RUNTIME_HEADER}"
  "${NATIVE_SHADOW_SETTINGS_HEADER}"
  "${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
  "${NATIVE_SHADOW_SETTINGS_STORE_HEADER}"
  "${NATIVE_SHADOW_PATCH_MODEL}"
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  "${VANILLA_SAO_BLUR_HEADER}"
  "${VANILLA_SAO_RAW_HEADER}"
  "${VANILLA_SSLR_BLUR_HEADER}"
  "${VANILLA_SSLR_PREPASS_HEADER}"
  "${VANILLA_SSLR_RAYTRACE_HEADER}"
  "${SOURCE_DIR}/support/Logger.h"
  "${SOURCE_DIR}/support/NearAllocation.h"
)
add_dependencies(${PROJECT_NAME}
  VanillaFixesShaders
  UpscalingShaders
  VanillaReflectionPatchTool
  ContactShadowShader
  SkylightingShaders
  FilmicTonemappingShader
  SubsurfaceScatteringShader
  ComplexParallaxShaders
  VerifyComplexParallax
  VerifyFo4VrLoader
  VerifyFo4VrRendererHook
  VerifyBSDFPrePassHook
  VerifyD3D11Hooks
  VerifyDlaa
  VerifyStreamlinePayload
  VerifyVanillaFixes
  VerifyNativeShadows
  VerifyIblFoundation
  VerifyIblCaptureProbeContracts
  VerifyIblMaterialContracts
  VerifyComplexMaterialProducerContracts
  VerifyComplexEnvironmentContracts
  VerifyContactShadows
  VerifySkylighting
  VerifyFilmicTonemapping
  VerifySurfaceClassification
  VerifyIblProjectionShaderAsset
  VerifyDistantTreeLinearLighting
  VerifyDistantTreeLinearLightingContracts
  VerifyParticleLinearLighting
  VerifyParticleLinearLightingContracts
  VerifyWaterLinearLighting
  VerifyWaterLinearLightingContracts
  VerifyVLSCompositeLinearLighting
  VerifyVLSCompositeLinearLightingContracts
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
  VerifyDevMenu)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${DLAA_GENERATED_DIR}"
  "${COMPLEX_PARALLAX_GENERATED_DIR}"
  "${CONTACT_SHADOW_GENERATED_DIR}"
  "${SKYLIGHTING_GENERATED_DIR}"
  "${FILMIC_TONEMAPPING_GENERATED_DIR}"
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}"
  "${VANILLA_FIXES_GENERATED_DIR}")
target_compile_definitions(${PROJECT_NAME} PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  NOMMNOSOUND
)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${SOURCE_DIR}"
  "${STREAMLINE_INCLUDE_DIR}"
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
  bcrypt
  dxguid
  shell32
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
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/Licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${STREAMLINE_RUNTIME_FILES}
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${STREAMLINE_LICENSE_FILES}
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/Licenses/"
  COMMAND "${CMAKE_COMMAND}" -E remove_directory
    "${ROOT_DIR}/package/PrismaUI_F4/views/FO4VR-Community-Shaders"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/DevMenu/Mods/${DEVMENU_PACKAGE_ID}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${DEVMENU_MANIFEST_SOURCE}"
    "${ROOT_DIR}/package/DevMenu/Mods/${DEVMENU_PACKAGE_ID}/menu.json"
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
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins/Streamline"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins/Streamline/Licenses"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${STREAMLINE_RUNTIME_FILES}
        "${copy_path}/F4SE/Plugins/Streamline/"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${STREAMLINE_LICENSE_FILES}
        "${copy_path}/F4SE/Plugins/Streamline/Licenses/"
      COMMAND "${CMAKE_COMMAND}" -E remove_directory
        "${copy_path}/PrismaUI_F4/views/FO4VR-Community-Shaders"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/DevMenu/Mods/${DEVMENU_PACKAGE_ID}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${DEVMENU_MANIFEST_SOURCE}"
        "${copy_path}/DevMenu/Mods/${DEVMENU_PACKAGE_ID}/menu.json"
      COMMENT "Deploying ${PROJECT_NAME} to '${copy_path}'"
    )
  endforeach()
endif()

option(BUILD_FO4VR_CS_TESTS
  "Build focused FO4VR Community Shaders tests." OFF)
if(BUILD_FO4VR_CS_TESTS)
  enable_testing()
  find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)

  add_executable(FO4VRCommunityShadersUpscalingSettingsTests
    "${ROOT_DIR}/tests/UpscalingSettingsTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersUpscalingSettingsTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersUpscalingSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersUpscalingSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.UpscalingSettings
    COMMAND FO4VRCommunityShadersUpscalingSettingsTests)

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

  add_executable(FO4VRCommunityShadersSettingsPathTests
    "${ROOT_DIR}/tests/SettingsPathTests.cpp"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersSettingsPathTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersSettingsPathTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersSettingsPathTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersSettingsPathTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersSettingsPathTests PRIVATE shell32)
  add_test(NAME FO4VRCommunityShaders.SettingsPath
    COMMAND FO4VRCommunityShadersSettingsPathTests)

  add_executable(FO4VRCommunityShadersSharedSettingsRuntimeTests
    "${ROOT_DIR}/tests/SharedSettingsRuntimeTests.cpp"
    "${SHARED_SETTINGS_RUNTIME_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersSharedSettingsRuntimeTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersSharedSettingsRuntimeTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersSharedSettingsRuntimeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.SharedSettingsRuntime
    COMMAND FO4VRCommunityShadersSharedSettingsRuntimeTests)

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

  add_executable(FO4VRCommunityShadersComplexMaterialProducerModelTests
    "${ROOT_DIR}/tests/ComplexMaterialProducerModelTests.cpp"
    "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
    "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
    "${COMPLEX_ENVIRONMENT_MODEL}"
    "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}")
  target_compile_features(
    FO4VRCommunityShadersComplexMaterialProducerModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersComplexMaterialProducerModelTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersComplexMaterialProducerModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.ComplexMaterialProducerModel
    COMMAND FO4VRCommunityShadersComplexMaterialProducerModelTests)

  add_test(NAME FO4VRCommunityShaders.ComplexMaterialProducerGenerator
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tests/test_generate_complex_material_producer_contracts.py")

  add_executable(FO4VRCommunityShadersComplexParallaxModelTests
    "${COMPLEX_PARALLAX_TEST_SOURCE}"
    "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
    "${COMPLEX_PARALLAX_MODEL_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersComplexParallaxModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersComplexParallaxModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersComplexParallaxModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.ComplexParallaxModel
    COMMAND FO4VRCommunityShadersComplexParallaxModelTests)

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
  add_test(NAME FO4VRCommunityShaders.DevMenuSource
    COMMAND "${CMAKE_COMMAND}"
      "-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
      "-DPROJECT_CMAKE_SOURCE=${ROOT_DIR}/CMakeLists.txt"
      "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
      "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
      "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
      "-DSOURCE_ROOT=${ROOT_DIR}"
      -P "${DEVMENU_VERIFY_SCRIPT}")

  add_test(NAME FO4VRCommunityShaders.D3D11HookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake'")

  add_test(NAME FO4VRCommunityShaders.VanillaFixesSource
    COMMAND "${CMAKE_COMMAND}"
      "-DVANILLA_SHADER_RUNTIME_SOURCE=${VANILLA_SHADER_RUNTIME_SOURCE}"
      "-DVANILLA_FOCUS_RUNTIME_SOURCE=${VANILLA_FOCUS_RUNTIME_SOURCE}"
      "-DVANILLA_RUNTIME_SOURCE=${VANILLA_RUNTIME_SOURCE}"
      "-DVANILLA_SETTINGS_STORE_SOURCE=${VANILLA_SETTINGS_STORE_SOURCE}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
      "-DVANILLA_REFLECTION_PATCH_SOURCE=${VANILLA_REFLECTION_PATCH_SOURCE}"
      "-DVANILLA_SSLR_ENVIRONMENT_SOURCE=${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
      "-DVANILLA_IBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
      "-DVANILLA_D3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
      "-DVANILLA_DEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}"
      "-DVANILLA_SHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
      "-DVANILLA_PLUGIN_SOURCE=${PLUGIN_SOURCE}"
      "-DVANILLA_IBL_GENERATOR_SOURCE=${VANILLA_IBL_GENERATOR_SOURCE}"
      "-DVANILLA_SAO_BLUR_SOURCE=${VANILLA_SAO_BLUR_SOURCE}"
      "-DVANILLA_SAO_RAW_SOURCE=${VANILLA_SAO_RAW_SOURCE}"
      "-DVANILLA_SSLR_BLUR_SOURCE=${VANILLA_SSLR_BLUR_SOURCE}"
      "-DVANILLA_SSLR_PREPASS_SOURCE=${VANILLA_SSLR_PREPASS_SOURCE}"
      "-DVANILLA_SSLR_RAYTRACE_SOURCE=${VANILLA_SSLR_RAYTRACE_SOURCE}"
      "-DVANILLA_FXC_EXECUTABLE=${FO4VR_CS_FXC_EXECUTABLE}"
      "-DVANILLA_SAO_BLUR_BINARY=${VANILLA_SAO_BLUR_BINARY}"
      "-DVANILLA_SAO_RAW_BINARY=${VANILLA_SAO_RAW_BINARY}"
      "-DVANILLA_SSLR_BLUR_BINARY=${VANILLA_SSLR_BLUR_BINARY}"
      "-DVANILLA_SSLR_PREPASS_BINARY=${VANILLA_SSLR_PREPASS_BINARY}"
      "-DVANILLA_SSLR_RAYTRACE_BINARY=${VANILLA_SSLR_RAYTRACE_BINARY}"
      -P "${VANILLA_VERIFY_SCRIPT}")

  add_test(NAME FO4VRCommunityShaders.ContactShadowsSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DCONTACT_SHADOW_RUNTIME_SOURCE=${CONTACT_SHADOW_RUNTIME_SOURCE}' '-DCONTACT_SHADOW_RUNTIME_HEADER=${CONTACT_SHADOW_RUNTIME_HEADER}' '-DCONTACT_SHADOW_SETTINGS_HEADER=${CONTACT_SHADOW_SETTINGS_HEADER}' '-DCONTACT_SHADOW_SETTINGS_STORE_SOURCE=${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}' '-DCONTACT_SHADOW_SETTINGS_STORE_HEADER=${CONTACT_SHADOW_SETTINGS_STORE_HEADER}' '-DCONTACT_SHADOW_SHADER_SOURCE=${CONTACT_SHADOW_SHADER_SOURCE}' '-DCONTACT_SHADOW_MASK_SHADER_SOURCE=${CONTACT_SHADOW_MASK_SHADER_SOURCE}' '-DCONTACT_SHADOW_DISPATCH_SHADER_SOURCE=${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}' '-DCONTACT_SHADOW_RESOLVE_SHADER_SOURCE=${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}' '-DCONTACT_SHADOW_SHADER_GENERATOR=${CONTACT_SHADOW_SHADER_GENERATOR}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}' '-DPLUGIN_SOURCE=${PLUGIN_SOURCE}' '-P' '${CONTACT_SHADOW_VERIFY_SCRIPT}'")

  add_executable(FO4VRCommunityShadersContactShadowSettingsTests
    "${ROOT_DIR}/tests/ContactShadowSettingsTests.cpp"
    "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    FO4VRCommunityShadersContactShadowSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.ContactShadowSettings
    COMMAND FO4VRCommunityShadersContactShadowSettingsTests)

  add_executable(FO4VRCommunityShadersVanillaFixesSettingsTests
    "${ROOT_DIR}/tests/VanillaFixesSettingsTests.cpp"
    "${VANILLA_SETTINGS_STORE_SOURCE}"
    "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.h"
    "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettings.h"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    FO4VRCommunityShadersVanillaFixesSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.VanillaFixesSettings
    COMMAND FO4VRCommunityShadersVanillaFixesSettingsTests)

  add_executable(FO4VRCommunityShadersNativeShadowSettingsTests
    "${ROOT_DIR}/tests/NativeShadowSettingsTests.cpp"
    "${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
    "${NATIVE_SHADOW_SETTINGS_STORE_HEADER}"
    "${NATIVE_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    FO4VRCommunityShadersNativeShadowSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.NativeShadowSettings
    COMMAND FO4VRCommunityShadersNativeShadowSettingsTests)

  add_test(NAME FO4VRCommunityShaders.NativeShadowsSource
    COMMAND "${CMAKE_COMMAND}"
      "-DNATIVE_SHADOW_RUNTIME_SOURCE=${NATIVE_SHADOW_RUNTIME_SOURCE}"
      "-DNATIVE_SHADOW_SETTINGS_HEADER=${NATIVE_SHADOW_SETTINGS_HEADER}"
      "-DNATIVE_SHADOW_SETTINGS_STORE_SOURCE=${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
      "-DNATIVE_SHADOW_PATCH_MODEL=${NATIVE_SHADOW_PATCH_MODEL}"
      "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
      -P "${NATIVE_SHADOW_VERIFY_SCRIPT}")

  add_executable(FO4VRCommunityShadersContactShadowShaderBytecodeTests
    "${ROOT_DIR}/tests/ContactShadowShaderBytecodeTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersContactShadowShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersContactShadowShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersContactShadowShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersContactShadowShaderBytecodeTests PRIVATE
    d3d11 d3dcompiler)
  add_dependencies(
    FO4VRCommunityShadersContactShadowShaderBytecodeTests ContactShadowShader)
  add_test(NAME FO4VRCommunityShaders.ContactShadowShaderBytecode
    COMMAND FO4VRCommunityShadersContactShadowShaderBytecodeTests
      "${CONTACT_SHADOW_SHADER_BINARY}"
      "${CONTACT_SHADOW_MASK_BINARY}")

  add_test(NAME FO4VRCommunityShaders.IblFoundationSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DIBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}' '-DIBL_RUNTIME_HEADER=${IBL_RUNTIME_HEADER}' '-DIBL_SETTINGS_HEADER=${IBL_SETTINGS_HEADER}' '-DIBL_SETTINGS_STORE_SOURCE=${IBL_SETTINGS_STORE_SOURCE}' '-DIBL_SETTINGS_STORE_HEADER=${IBL_SETTINGS_STORE_HEADER}' '-DIBL_PROVIDER_MODEL=${IBL_PROVIDER_MODEL}' '-DIBL_ENVIRONMENT_PROVIDER_SOURCE=${IBL_ENVIRONMENT_PROVIDER_SOURCE}' '-DIBL_ENVIRONMENT_PROVIDER_HEADER=${IBL_ENVIRONMENT_PROVIDER_HEADER}' '-DIBL_ENVIRONMENT_UPDATER_SOURCE=${IBL_ENVIRONMENT_UPDATER_SOURCE}' '-DIBL_ENVIRONMENT_UPDATER_HEADER=${IBL_ENVIRONMENT_UPDATER_HEADER}' '-DIBL_COMPUTE_STATE_SCOPE_SOURCE=${IBL_COMPUTE_STATE_SCOPE_SOURCE}' '-DIBL_COMPUTE_STATE_SCOPE_HEADER=${IBL_COMPUTE_STATE_SCOPE_HEADER}' '-DIBL_PROJECTION_MODEL=${IBL_PROJECTION_MODEL}' '-DIBL_CAPTURE_PROBE_MODEL=${IBL_CAPTURE_PROBE_MODEL}' '-DIBL_SCENE_RADIANCE_PROBE_MODEL=${IBL_SCENE_RADIANCE_PROBE_MODEL}' '-DIBL_REFLECTION_FREE_CAPTURE_SOURCE=${IBL_REFLECTION_FREE_CAPTURE_SOURCE}' '-DIBL_REFLECTION_FREE_CAPTURE_HEADER=${IBL_REFLECTION_FREE_CAPTURE_HEADER}' '-DIBL_MATERIAL_BINDING_SCOPE_SOURCE=${IBL_MATERIAL_BINDING_SCOPE_SOURCE}' '-DIBL_MATERIAL_BINDING_SCOPE_HEADER=${IBL_MATERIAL_BINDING_SCOPE_HEADER}' '-DIBL_MATERIAL_GENERATED_CONTRACTS=${IBL_MATERIAL_GENERATED_CONTRACTS}' '-DIBL_PROJECTION_SHADER_SOURCE=${IBL_PROJECTION_SHADER_SOURCE}' '-DIBL_PROJECTION_SHADER_ASSET=${IBL_PROJECTION_SHADER_ASSET}' '-DIBL_ENVIRONMENT_UPDATE_SHADER_SOURCE=${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}' '-DIBL_ENVIRONMENT_UPDATE_SHADER_ASSET=${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}' '-DIBL_ENVIRONMENT_FILTER_SHADER_SOURCE=${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}' '-DIBL_ENVIRONMENT_FILTER_SHADER_ASSET=${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DGEOMETRY_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}' '-DDEVMENU_MANIFEST_SOURCE=${DEVMENU_MANIFEST_SOURCE}' '-DPLUGIN_SOURCE=${PLUGIN_SOURCE}' '-DRESOURCE_SOURCE=${RESOURCE_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyIblFoundation.cmake'")

  add_test(NAME FO4VRCommunityShaders.IblProjectionShaderAsset
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_PROJECTION_SHADER_VERIFIER}"
      --root "${ROOT_DIR}"
      --check)

  add_test(NAME FO4VRCommunityShaders.IblCaptureProbeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_CAPTURE_PROBE_VERIFIER}"
      --root "${ROOT_DIR}")

  add_test(NAME FO4VRCommunityShaders.IblMaterialContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_MATERIAL_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${IBL_MATERIAL_GENERATED_CONTRACTS}"
      --surface-anchor-tool "$<TARGET_FILE:VanillaReflectionPatchTool>"
      --check)

  add_test(NAME FO4VRCommunityShaders.ComplexMaterialProducerContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
      --check)

  add_test(NAME FO4VRCommunityShaders.ComplexEnvironmentContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${COMPLEX_ENVIRONMENT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
      --check)

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

  add_executable(FO4VRCommunityShadersIblProjectionTests
    "${ROOT_DIR}/tests/IblProjectionTests.cpp"
    "${IBL_PROJECTION_MODEL}")
  target_compile_features(
    FO4VRCommunityShadersIblProjectionTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblProjectionTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblProjectionTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblProjectionTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblProjectionTests PRIVATE d3d11)
  add_test(NAME FO4VRCommunityShaders.IblProjection
    COMMAND FO4VRCommunityShadersIblProjectionTests "${ROOT_DIR}")

  add_executable(FO4VRCommunityShadersIblCaptureProbeModelTests
    "${ROOT_DIR}/tests/IblCaptureProbeModelTests.cpp"
    "${IBL_CAPTURE_PROBE_MODEL}")
  target_compile_features(
    FO4VRCommunityShadersIblCaptureProbeModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersIblCaptureProbeModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblCaptureProbeModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.IblCaptureProbeModel
    COMMAND FO4VRCommunityShadersIblCaptureProbeModelTests)

  add_executable(FO4VRCommunityShadersIblSceneRadianceProbeModelTests
    "${ROOT_DIR}/tests/IblSceneRadianceProbeModelTests.cpp"
    "${IBL_SCENE_RADIANCE_PROBE_MODEL}")
  target_compile_features(
    FO4VRCommunityShadersIblSceneRadianceProbeModelTests PRIVATE cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersIblSceneRadianceProbeModelTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblSceneRadianceProbeModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME FO4VRCommunityShaders.IblSceneRadianceProbeModel
    COMMAND FO4VRCommunityShadersIblSceneRadianceProbeModelTests)

  add_executable(FO4VRCommunityShadersIblReflectionFreeCaptureTests
    "${ROOT_DIR}/tests/IblReflectionFreeCaptureTests.cpp"
    "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
    "${GPU_TIMING_PROFILER_SOURCE}"
    "${GPU_TIMING_PROFILER_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersIblReflectionFreeCaptureTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblReflectionFreeCaptureTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblReflectionFreeCaptureTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblReflectionFreeCaptureTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblReflectionFreeCaptureTests PRIVATE
    d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.IblReflectionFreeCapture
    COMMAND FO4VRCommunityShadersIblReflectionFreeCaptureTests)

  add_executable(FO4VRCommunityShadersIblMaterialBindingScopeTests
    "${ROOT_DIR}/tests/IblMaterialBindingScopeTests.cpp"
    "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
    "${IBL_MATERIAL_BINDING_SCOPE_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersIblMaterialBindingScopeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblMaterialBindingScopeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblMaterialBindingScopeTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblMaterialBindingScopeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblMaterialBindingScopeTests PRIVATE d3d11)
  add_test(NAME FO4VRCommunityShaders.IblMaterialBindingScope
    COMMAND FO4VRCommunityShadersIblMaterialBindingScopeTests)

  add_executable(FO4VRCommunityShadersIblMaterialShaderBytecodeTests
    "${ROOT_DIR}/tests/IblMaterialShaderBytecodeTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersIblMaterialShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblMaterialShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersIblMaterialShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblMaterialShaderBytecodeTests PRIVATE d3d11)
  add_test(NAME FO4VRCommunityShaders.IblMaterialShaderBytecode
    COMMAND FO4VRCommunityShadersIblMaterialShaderBytecodeTests "${ROOT_DIR}")

  add_executable(FO4VRCommunityShadersIblProviderTests
    "${ROOT_DIR}/tests/IblProviderTests.cpp"
    "${IBL_PROVIDER_MODEL}"
    "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersIblProviderTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblProviderTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblProviderTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblProviderTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblProviderTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.IblProvider
    COMMAND FO4VRCommunityShadersIblProviderTests)

  add_executable(FO4VRCommunityShadersIblEnvironmentUpdaterTests
    "${ROOT_DIR}/tests/IblEnvironmentUpdaterTests.cpp"
    "${IBL_PROVIDER_MODEL}"
    "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
    "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
    "${IBL_ENVIRONMENT_UPDATER_HEADER}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
    "${GPU_TIMING_PROFILER_SOURCE}"
    "${GPU_TIMING_PROFILER_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersIblEnvironmentUpdaterTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblEnvironmentUpdaterTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblEnvironmentUpdaterTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblEnvironmentUpdaterTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersIblEnvironmentUpdaterTests PRIVATE
    d3d11 spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.IblEnvironmentUpdater
    COMMAND FO4VRCommunityShadersIblEnvironmentUpdaterTests "${ROOT_DIR}")

  add_executable(FO4VRCommunityShadersIblSettingsTests
    "${ROOT_DIR}/tests/IblSettingsTests.cpp"
    "${IBL_SETTINGS_STORE_SOURCE}"
    "${IBL_SETTINGS_STORE_HEADER}"
    "${IBL_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    FO4VRCommunityShadersIblSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    FO4VRCommunityShadersIblSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    FO4VRCommunityShadersIblSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    FO4VRCommunityShadersIblSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    FO4VRCommunityShadersIblSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    FO4VRCommunityShadersIblSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME FO4VRCommunityShaders.IblSettings
    COMMAND FO4VRCommunityShadersIblSettingsTests)

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
  add_test(NAME FO4VRCommunityShaders.VLSCompositeLinearLightingRuntimeContract
    COMMAND "${Python3_EXECUTABLE}"
      "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
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
  add_executable(FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/VLSCompositeLinearLightingShaderParityTests.cpp")
  target_compile_features(
    FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests PRIVATE
    FO4VRCommunityShadersCore d3d11 d3dcompiler)
  add_test(NAME FO4VRCommunityShaders.VLSCompositeLinearLightingShaderParity
    COMMAND FO4VRCommunityShadersVLSCompositeLinearLightingShaderParityTests
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
