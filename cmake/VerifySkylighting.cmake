foreach(variable IN ITEMS
    SKYLIGHTING_RUNTIME_SOURCE
    SKYLIGHTING_RUNTIME_HEADER
    SKYLIGHTING_NATIVE_HOOK_SOURCE
    SKYLIGHTING_SETTINGS_HEADER
    SKYLIGHTING_SETTINGS_STORE_SOURCE
    SKYLIGHTING_AMBIENT_SHADER_SOURCE
    SKYLIGHTING_COMPUTE_SHADER_SOURCE
    SKYLIGHTING_SHADER_GENERATOR
    SKYLIGHTING_AMBIENT_HEADER
    D3D11_HOOK_SOURCE
    LINEAR_LIGHTING_RUNTIME_SOURCE
    SHARED_SETTINGS_SOURCE
    SHARED_SETTINGS_HEADER
    PLUGIN_SOURCE
    DEVMENU_MANIFEST_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "Skylighting verification input is missing: ${variable}")
  endif()
endforeach()

file(READ "${SKYLIGHTING_RUNTIME_SOURCE}" runtime)
file(READ "${SKYLIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${SKYLIGHTING_NATIVE_HOOK_SOURCE}" nativeHook)
file(READ "${SKYLIGHTING_SETTINGS_HEADER}" settingsHeader)
file(READ "${SKYLIGHTING_SETTINGS_STORE_SOURCE}" settingsStore)
file(READ "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}" ambientShader)
file(READ "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}" computeShader)
file(READ "${SKYLIGHTING_SHADER_GENERATOR}" generator)
file(READ "${SKYLIGHTING_AMBIENT_HEADER}" ambientHeader)
file(READ "${D3D11_HOOK_SOURCE}" d3dHook)
file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" linearRuntime)
file(READ "${SHARED_SETTINGS_SOURCE}" sharedSettings)
file(READ "${SHARED_SETTINGS_HEADER}" sharedSettingsHeader)
file(READ "${PLUGIN_SOURCE}" plugin)
file(READ "${DEVMENU_MANIFEST_SOURCE}" devMenu)

foreach(required IN ITEMS
    "kWrapperRva = 0x00634300"
    "kRenderRva = 0x006350C0"
    "kProjectionRva = 0x00635530"
    "kCubeSizeRva = 0x05A3CFA4"
    "kDirectionRva = 0x05A3CFC8"
    "kWrapperFirstCallTargetRva = 0x0012FB50"
    "kPrecipitationManagerOffset = 0xA0"
    "std::array<std::byte, 6> kWrapperSignature"
    "std::byte{ 0x40 }, std::byte{ 0x53 }"
    "wrapper + kWrapperSignature.size()"
    "resolveNativePrecipitationManager()"
    "nativeRendererSingleton()"
    "MH_CreateHook("
    "captureDetourIdentity("
    "Runtime::get().setNativeHookOwned(true)")
  string(FIND "${nativeHook}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting native identity contract is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "return { 64, 64, 32 }"
    "return { 128, 128, 64 }"
    "return { 256, 256, 128 }"
    "DXGI_FORMAT_R16G16B16A16_FLOAT"
    "DXGI_FORMAT_R8_UINT"
    "ScopedComputeState"
    "substituteDepthStencil("
    "InterlockedIncrement("
    "nativeOutput_.data() + kNativeProjectionOffset"
    "PSSetShaderResources(50"
    "PSSetConstantBuffers(13"
    "probeDataValid_.store(true"
    "firstActiveAmbientBindLogged_.exchange(")
  string(FIND "${runtime}\n${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting runtime contract is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Quality quality{ Quality::high }"
    "bEnabled"
    "iQuality"
    "fMinDiffuseVisibility"
    "fMinSpecularVisibility"
    "fMaxZenithDegrees")
  string(FIND "${settingsHeader}\n${settingsStore}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting settings contract is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "register(t50)"
    "register(b13)"
    "Texture3D<float4> SkylightingProbeArray"
    "ReconstructRelativeWorldPosition"
    "FauxSpecularLobe"
    "output.DiffuseVisibility"
    "output.SpecularVisibility")
  string(FIND "${ambientShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting ambient shader is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "register(t0)"
    "register(u0)"
    "register(u1)"
    "register(s0)"
    "register(b13)"
    "[numthreads(8, 8, 1)]"
    "min(previousFrames + 1u, 255u)")
  string(FIND "${computeShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting compute shader is missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL
  "Fo4vrCsSkylightingAmbientContract\\{"
  ambientContracts "${ambientHeader}")
list(LENGTH ambientContracts ambientContractCount)
if(NOT ambientContractCount EQUAL 39)
  message(FATAL_ERROR
    "Skylighting ambient coverage changed: expected 39 contracts, found ${ambientContractCount}")
endif()

foreach(required IN ITEMS
    "SkylightingAmbient.h"
    "fo4vr_cs_skylighting_ambient_contracts"
    "skylightingContract.gammaOffsets"
    "selectDFLightAmbientPixelShader")
  string(FIND "${linearRuntime}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting ambient owner integration is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylightingRuntime.substituteDepthStencil"
    "skylightingRuntime.privateCaptureActive()"
    "skylighting::Runtime::get().scopeAmbientDraw"
    "skylighting::installNativeHooks()")
  string(FIND "${d3dHook}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting D3D11 integration is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylighting::Runtime::get().applySettings"
    ".skylighting = skylighting::loadSettings(path)")
  string(FIND "${sharedSettings}\n${sharedSettingsHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting shared settings publication is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylighting::loadSettings()"
    "skylighting::validateNativeHooks("
    ".beginWorldSession()")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting plugin lifecycle is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "\"id\": \"skylighting\""
    "\"id\": \"skylighting-quality\""
    "\"id\": \"skylighting-diffuse-min\""
    "\"id\": \"skylighting-specular-min\""
    "\"id\": \"skylighting-zenith\"")
  string(FIND "${devMenu}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting DevMenu contract is missing '${required}'")
  endif()
endforeach()

message(STATUS
  "Verified Skylighting: exact FO4VR native capture, shared stereo probes, 39 ambient contracts, and DevMenu ownership")
