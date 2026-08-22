function(vanilla_fixes_require_file variable_name)
  if(NOT DEFINED ${variable_name} OR NOT EXISTS "${${variable_name}}")
    message(FATAL_ERROR
      "Vanilla Fixes verification input '${variable_name}' is missing")
  endif()
endfunction()

function(vanilla_fixes_require_text source label)
  foreach(required IN LISTS ARGN)
    string(FIND "${source}" "${required}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR
        "Vanilla Fixes ${label} regression: missing '${required}'")
    endif()
  endforeach()
endfunction()

foreach(input IN ITEMS
    VANILLA_SHADER_RUNTIME_SOURCE
    VANILLA_FOCUS_RUNTIME_SOURCE
    VANILLA_RUNTIME_SOURCE
    VANILLA_SETTINGS_STORE_SOURCE
    VANILLA_REFLECTION_PATCH_SOURCE
    VANILLA_SSLR_ENVIRONMENT_SOURCE
    VANILLA_IBL_RUNTIME_SOURCE
    VANILLA_D3D11_HOOK_SOURCE
    VANILLA_DEVMENU_MANIFEST_SOURCE
    VANILLA_SHARED_SETTINGS_SOURCE
    VANILLA_PLUGIN_SOURCE
    VANILLA_IBL_GENERATOR_SOURCE
    VANILLA_SAO_BLUR_SOURCE
    VANILLA_SAO_RAW_SOURCE
    VANILLA_SSLR_BLUR_SOURCE
    VANILLA_SSLR_PREPASS_SOURCE
    VANILLA_SSLR_RAYTRACE_SOURCE)
  vanilla_fixes_require_file(${input})
endforeach()

file(READ "${VANILLA_SHADER_RUNTIME_SOURCE}" shader_runtime)
vanilla_fixes_require_text("${shader_runtime}" "shader identity"
  "kSaoRawAo"
  "3892"
  "0x4A8B8CB64AC24499ull"
  "kSaoHorizontalBlur"
  "2324"
  "0x5186B7FAB41E51CEull"
  "kSslrHorizontalBlur"
  "800"
  "0x45D4CB8E6B37F5E7ull"
  "kSslrPrepass"
  "3080"
  "0xBB9FDCC3817DC31Cull"
  "kSslrRaytrace"
  "68624"
  "0xE4DB1ED97A719E55ull"
  "kFocusShadow"
  "19504"
  "0xF4EBA56324D74051ull"
  "CompleteIdentity{ 9348, 0x59FAED17411F08C7ull"
  "CompleteIdentity{ 9564, 0x0903D20AD75EBB0Eull"
  "CompleteIdentity{ 11100, 0x81247323F5EF60D3ull"
  "CompleteIdentity{ 11316, 0x29D9E8483ACB1988ull"
  "identity.hasDxbcHeader"
  "identity.bytecodeSize == expected.size"
  "identity.hash == expected.hash"
  "identity.checksum == expected.checksum"
  "patchStockReflectionCompositeSurfaceAnchoredCubemap"
  "fo4vr_cs_vanilla_sslr_raytrace_ps"
  "publishSslrPixelShaderPair"
  "selectSslrPixelShaderForBinding")

file(READ "${VANILLA_FOCUS_RUNTIME_SOURCE}" focus_runtime)
vanilla_fixes_require_text("${focus_runtime}" "focus-shadow native/resource"
  "kProducerRva = 0x29126C0"
  "kTaskPrepareRva = 0x28AAC40"
  "kMapPublishRva = 0x2912500"
  "kMapTaskExecuteRva = 0x28CB6C0"
  "exactEntry(targets[0], kProducerEntry)"
  "exactEntry(targets[1], kTaskPrepareEntry)"
  "exactEntry(targets[2], kMapPublishEntry)"
  "exactEntry(targets[3], kMapTaskExecuteEntry)"
  "kShadowMapSlot = 5"
  "kShadowMapWidth = 8192"
  "kShadowMapHeight = 8192"
  "kShadowMapArraySize = 4"
  "DXGI_FORMAT_D16_UNORM"
  "DXGI_FORMAT_R16_TYPELESS"
  "DXGI_FORMAT_R16_UNORM"
  "D3D11_DSV_DIMENSION_TEXTURE2DARRAY"
  "D3D11_SRV_DIMENSION_TEXTURE2DARRAY"
  "CreateShaderResourceView"
  "PSGetShaderResources"
  "PSSetShaderResources"
  "ScopedFocusShadowBinding::~ScopedFocusShadowBinding")

file(READ "${VANILLA_RUNTIME_SOURCE}" runtime)
vanilla_fixes_require_text("${runtime}" "engine-gate"
  "0x03740E38"
  "0x037C69F8"
  "0x037C6A10"
  "0x037C76E8"
  "0x03924D50"
  "0x03924D68"
  "0x03924EB8"
  "0x03924ED0"
  "0x039255F0"
  "0x03925608"
  "kRendererConfigRva = 0x068787F0"
  "kImageSpaceManagerPointerRva = 0x068789E8"
  "kSunbeamsAvailabilityRva = 0x0689AC94"
  "kSaoEffectVtableRva = 0x030B8FD8"
  "saoEffectIndex = 0x47"
  "validateRipTarget"
  "writableRange"
  "InterlockedExchange8"
  "preserveStartupCapability"
  "setSslrSuiteRequested("
  "kPollInterval = std::chrono::milliseconds(250)"
  "reloadIfChanged()")
foreach(forbidden IN ITEMS "REL::Relocation" "REL::ID" "Data/F4SE/Plugins")
  string(FIND "${runtime}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Vanilla Fixes engine-gate contains forbidden '${forbidden}'")
  endif()
endforeach()

file(READ "${VANILLA_SETTINGS_STORE_SOURCE}" settings_store)
vanilla_fixes_require_text("${settings_store}" "settings"
  "kSection = L\"VanillaFixes\""
  "L\"bEnabled\""
  "L\"bPrecipitationOcclusion\""
  "L\"bAllowImageSpaceModifiers\""
  "L\"bVrAllowSAO\""
  "L\"bVrAllowScreenSpaceReflections\""
  "L\"bVrAllowScreenSpaceSubsurfaceScattering\""
  "L\"bLensFlareVr\""
  "L\"bVrAllowFocusShadows\""
  "L\"bUseSunbeams\""
  "settings_path::resolveIniPath()")

file(READ "${VANILLA_REFLECTION_PATCH_SOURCE}" reflection_patch)
vanilla_fixes_require_text("${reflection_patch}" "reflection transform"
  "patchStockReflectionCompositeSurfaceAnchoredCubemap"
  "recomputeDxbcChecksum"
  "patchedBytecode.swap(candidate)")
file(READ "${VANILLA_SSLR_ENVIRONMENT_SOURCE}" sslr_environment)
vanilla_fixes_require_text("${sslr_environment}" "SSLR environment binding"
  "kFirstResourceSlot = 4"
  "kResourceCount = 4"
  "kSamplerSlot = 4"
  "kConstantSlot = 11"
  "kCurrentHitMinimumConfidence = 0.65f"
  "tryGetSslrEnvironment"
  "PSGetShaderResources"
  "PSSetShaderResources"
  "PSGetConstantBuffers"
  "PSSetConstantBuffers"
  "appliedMatches"
  "(void)restore()"
  "setSslrConsumerEnabled")
file(READ "${VANILLA_IBL_RUNTIME_SOURCE}" ibl_runtime)
vanilla_fixes_require_text("${ibl_runtime}" "shared IBL SSLR consumer"
  "sslrConsumerEnabled_"
  "setSslrConsumerEnabled"
  "tryGetSslrEnvironment"
  "materialEnvironmentTransitionWeight_"
  "publishedEnvironmentSessionId_ == requestedSession")

file(READ "${VANILLA_D3D11_HOOK_SOURCE}" d3d11)
vanilla_fixes_require_text("${d3d11}" "D3D11 ownership"
  "hookCreateComputeShader"
  "vanilla_fixes::selectVertexShader("
  "vanilla_fixes::selectPixelShader("
  "vanilla_fixes::selectComputeShader("
  "vanilla_fixes::selectSslrPixelShaderForBinding(shader)"
  "vanilla_fixes::isSslrRaytracePixelShader(shader)"
  "vanilla_fixes::retainedStockSslrPixelShader(shader)"
  "reconcileSslrDrawShader(context, sslrEnvironment)"
  "ScopedSslrEnvironmentBinding"
  "observeFocusShadowRenderTargets(depthStencil)"
  "vanilla_fixes::isFocusShadowPixelShader(shader)"
  "ScopedFocusShadowBinding focusShadowBinding"
  "installFocusShadowNativeHooks()")
string(REGEX MATCHALL
  "ScopedFocusShadowBinding focusShadowBinding" focus_draw_bindings "${d3d11}")
list(LENGTH focus_draw_bindings focus_draw_binding_count)
if(NOT focus_draw_binding_count EQUAL 4)
  message(FATAL_ERROR
    "Vanilla Fixes focus resource must bind at all four draw boundaries")
endif()
string(REGEX MATCHALL
  "sslrEnvironment\\(" sslr_draw_bindings "${d3d11}")
list(LENGTH sslr_draw_bindings sslr_draw_binding_count)
if(NOT sslr_draw_binding_count EQUAL 4)
  message(FATAL_ERROR
    "Vanilla Fixes SSLR environment must bind at all four draw boundaries")
endif()

file(READ "${VANILLA_DEVMENU_MANIFEST_SOURCE}" devmenu)
vanilla_fixes_require_text("${devmenu}" "DevMenu controls"
  "\"id\": \"vanilla-fixes\""
  "\"section\": \"VanillaFixes\""
  "\"key\": \"bEnabled\""
  "\"key\": \"bPrecipitationOcclusion\""
  "\"key\": \"bAllowImageSpaceModifiers\""
  "\"key\": \"bVrAllowSAO\""
  "\"key\": \"bVrAllowScreenSpaceReflections\""
  "\"key\": \"bVrAllowScreenSpaceSubsurfaceScattering\""
  "\"key\": \"bLensFlareVr\""
  "\"key\": \"bVrAllowFocusShadows\""
  "\"key\": \"bUseSunbeams\"")
file(READ "${VANILLA_SHARED_SETTINGS_SOURCE}" shared_settings)
vanilla_fixes_require_text("${shared_settings}" "live settings publication"
  "vanilla_fixes::applySettings(next.vanillaFixes)")

file(READ "${VANILLA_PLUGIN_SOURCE}" plugin)
vanilla_fixes_require_text("${plugin}" "startup"
  "vanilla_fixes::loadSettings()"
  "vanilla_fixes::startRuntime("
  "render::installEarlyD3D11Hooks()")
string(FIND "${plugin}" "vanilla_fixes::startRuntime(" runtime_start)
string(FIND "${plugin}" "render::installEarlyD3D11Hooks()" d3d_start)
if(runtime_start EQUAL -1 OR d3d_start EQUAL -1 OR
   NOT runtime_start LESS d3d_start)
  message(FATAL_ERROR
    "Vanilla Fixes engine-gate runtime must start before D3D interception")
endif()

file(READ "${VANILLA_IBL_GENERATOR_SOURCE}" ibl_generator)
vanilla_fixes_require_text("${ibl_generator}" "IBL composition"
  "--surface-anchor-tool"
  "len(SURFACE_ANCHORED_IDENTITIES)"
  "anchored_count"
  "9348"
  "9564"
  "11100"
  "11316")

file(READ "${VANILLA_SAO_BLUR_SOURCE}" sao_blur)
vanilla_fixes_require_text("${sao_blur}" "SAO blur source"
  "kTileOutputWidth = 960"
  "kFilterRadius = 6"
  "width >> 1u"
  "sampleX = eyeBoundary - 1"
  "sampleX = eyeBoundary")
file(READ "${VANILLA_SAO_RAW_SOURCE}" sao_raw)
vanilla_fixes_require_text("${sao_raw}" "raw SAO source"
  "eyeLocalX * 2 + 1"
  "clampInternalEyeSampleX"
  "eyeLocalUvX"
  "sampleIndex < 5"
  "motionDirection.x * 0.5f"
  "motion.x * 0.5f"
  "clampInternalEyeUv"
  "depthAgreement * motionAgreement * 0.99f")
file(READ "${VANILLA_SSLR_BLUR_SOURCE}" sslr_blur)
vanilla_fixes_require_text("${sslr_blur}" "SSLR blur source"
  "centerX >= 0.5f"
  "0.5f + halfTexel"
  "0.5f - halfTexel"
  "-3.294215f"
  "3.294215f")
file(READ "${VANILLA_SSLR_PREPASS_SOURCE}" sslr_prepass)
vanilla_fixes_require_text("${sslr_prepass}" "SSLR prepass source"
  "depth <= 0.01f"
  "input.uv.x >= 0.5f"
  "eyeMatrixOffset = rightEye ? 4u : 0u"
  "eyeLocalX ="
  "rayClipPosition"
  "geometricCandidate = cross("
  "ddx(viewPosition)"
  "ddy(viewPosition)"
  "worldNormal = transformRows(20u, normal)"
  "reflectedWorld = normalize(reflect("
  "encodeDirection(reflectedWorld)")
foreach(forbidden IN ITEMS
    "reflected.z >"
    "dot(normal, viewDirection) >= 0.0f"
    "eyeLocalEndpoint"
    "reflected * 1000.0f")
  string(FIND "${sslr_prepass}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Vanilla Fixes SSLR prepass retained stale '${forbidden}'")
  endif()
endforeach()

file(READ "${VANILLA_SSLR_RAYTRACE_SOURCE}" sslr_raytrace)
vanilla_fixes_require_text("${sslr_raytrace}" "SSLR raytrace source"
  "CameraData[85]"
  "PublishedEnvironment : register(t4)"
  "PreviousEnvironment : register(t6)"
  "PreviousValidity : register(t7)"
  "SslrEnvironmentParameters : register(b11)"
  "DirectionTexture.Load"
  "ViewDepthTexture.Load"
  "reconstructLinearPosition"
  "viewDepth / eyeRay.z"
  "loadWorldRadiance"
  "maximumSteps = 32u"
  "refinementSteps = 5u"
  "maximumTravel = 1000.0f"
  "return rightEye ? uv.x > 0.5f : uv.x < 0.5f"
  "confidence >= 1.0f - 1.0e-5f"
  "confidence - EnvironmentControl.z"
  "return float4(worldRadiance, worldValidity)")

if(DEFINED VANILLA_FXC_EXECUTABLE)
  vanilla_fixes_require_file(VANILLA_FXC_EXECUTABLE)
  foreach(shader IN ITEMS
      SAO_BLUR SAO_RAW SSLR_BLUR SSLR_PREPASS SSLR_RAYTRACE)
    vanilla_fixes_require_file(VANILLA_${shader}_BINARY)
  endforeach()

  set(binary_specs
    "VANILLA_SAO_BLUR_BINARY|3564|d390578f8f6cea606fed706fcc041c421c5d724e48536544fda83d702feaada9|cs_5_0|dcl_thread_group 972, 1, 1"
    "VANILLA_SAO_RAW_BINARY|5936|5a0373b4ac810c4abc392d3887d4bb916a798de6aa63bff140a9d0f49e6a53ee|cs_5_0|dcl_thread_group 16, 16, 1"
    "VANILLA_SSLR_BLUR_BINARY|1532|cd5a5f6c4f2faf238403ca8bc366a00557f39f373c22b6cdbbc67588c5e0e25d|vs_5_0|dcl_output o5.xy"
    "VANILLA_SSLR_PREPASS_BINARY|4640|a7526cfc9c67cf62f2719dd7882cef2388c4943d2f22010ddae5c8b8d66c9f80|ps_5_0|dynamicIndexed"
    "VANILLA_SSLR_RAYTRACE_BINARY|13312|ee415ddfd2e88de2cbca774651c9ed9d82401841c03200268bee80f571454596|ps_5_0|dcl_resource_texturecube")
  foreach(spec IN LISTS binary_specs)
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 path_variable)
    list(GET fields 1 expected_size)
    list(GET fields 2 expected_sha)
    list(GET fields 3 profile_pattern)
    list(GET fields 4 contract_pattern)
    file(SIZE "${${path_variable}}" actual_size)
    file(SHA256 "${${path_variable}}" actual_sha)
    if(NOT actual_size EQUAL expected_size OR
       NOT actual_sha STREQUAL expected_sha)
      message(FATAL_ERROR
        "Vanilla Fixes shader identity changed for ${path_variable}: size=${actual_size}, sha256=${actual_sha}")
    endif()
    execute_process(
      COMMAND "${VANILLA_FXC_EXECUTABLE}" /dumpbin /nologo "${${path_variable}}"
      RESULT_VARIABLE dump_result
      OUTPUT_VARIABLE assembly
      ERROR_VARIABLE dump_error)
    if(NOT dump_result EQUAL 0 OR
       NOT assembly MATCHES "${profile_pattern}" OR
       NOT assembly MATCHES "${contract_pattern}")
      message(FATAL_ERROR
        "Vanilla Fixes shader contract failed for ${path_variable}: ${dump_error}")
    endif()
    if(path_variable STREQUAL "VANILLA_SSLR_RAYTRACE_BINARY")
      foreach(required_pattern IN ITEMS
          "dcl_constantbuffer CB11\\[1\\]"
          "dcl_constantbuffer CB12\\[40\\], dynamicIndexed"
          "dcl_sampler s3"
          "dcl_sampler s4"
          "dcl_resource_texture2d.* t0"
          "dcl_resource_texture2d.* t1"
          "dcl_resource_texture2d.* t2"
          "dcl_resource_texture2d.* t3"
          "dcl_resource_texturecube.* t4"
          "dcl_resource_texturecube.* t5"
          "dcl_resource_texturecube.* t6"
          "dcl_resource_texturecube.* t7"
          "dcl_output o0.xyzw"
          "loop")
        if(NOT assembly MATCHES "${required_pattern}")
          message(FATAL_ERROR
            "Vanilla Fixes SSLR raytrace bytecode is missing '${required_pattern}'")
        endif()
      endforeach()
      foreach(forbidden_pattern IN ITEMS
          "dcl_sampler s[0-2]"
          "dcl_sampler s[5-9]"
          "dcl_resource_[^\n]* t[89]"
          "dcl_resource_[^\n]* t1[0-9]"
          "dcl_uav")
        if(assembly MATCHES "${forbidden_pattern}")
          message(FATAL_ERROR
            "Vanilla Fixes SSLR raytrace bytecode contains forbidden '${forbidden_pattern}'")
        endif()
      endforeach()
    endif()
  endforeach()
endif()

message(STATUS "Verified Vanilla Fixes native, shader, settings, and DevMenu contracts")
