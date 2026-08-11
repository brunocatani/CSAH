foreach(variable IN ITEMS
    IBL_RUNTIME_SOURCE
    IBL_RUNTIME_HEADER
    IBL_PROJECTION_MODEL
    IBL_SCENE_RADIANCE_PROBE_MODEL
    IBL_PROJECTION_SHADER_SOURCE
    IBL_PROJECTION_SHADER_ASSET
    D3D11_HOOK_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${IBL_RUNTIME_SOURCE}" runtimeSource)
file(READ "${IBL_RUNTIME_HEADER}" runtimeHeader)
file(READ "${IBL_PROJECTION_MODEL}" projectionModel)
file(READ "${IBL_SCENE_RADIANCE_PROBE_MODEL}" sceneProbeModel)
file(READ "${IBL_PROJECTION_SHADER_SOURCE}" shaderSource)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "kNativeCubemapSrvRva = 0x0623C3C0"
    "GetModuleHandleW(nullptr)"
    "D3D11_RESOURCE_MISC_TEXTURECUBE"
    "D3D11_SRV_DIMENSION_TEXTURECUBE"
    "D3D11_MAP_FLAG_DO_NOT_WAIT"
    "class ScopedComputeState"
    "CSGetShaderResources"
    "CSGetUnorderedAccessViews"
    "CSGetSamplers"
    "Dispatch(1, 1, 1)"
    "CopyResource"
    "onDFLightAmbientBind"
    "matchCaptureProbeContract"
    "PSGetShaderResources"
    "PSGetConstantBuffers(12, 1"
    "OMGetRenderTargets"
    "RSGetViewports"
    "state read only, image unchanged")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSceneRadianceCandidateT5Slot = 5"
    "kSceneRadianceCandidateT6Slot = 6"
    "kSceneRadianceMaximumReadbackPolls = 80"
    "CopySubresourceRegion"
    "createSceneRadianceProbeResources"
    "consumeSceneRadianceProbeReadbacks"
    "D3D11_MAP_FLAG_DO_NOT_WAIT"
    "IBL scene-radiance OM-before:"
    "\"PS-t5\""
    "\"PS-t6\""
    "IBL scene-radiance OM-after:"
    "onCaptureProbeDrawComplete")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL scene-radiance probe regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSceneProbeSampleCount = 8"
    "sceneProbeCoordinates"
    "decodeR11G11B10Float"
    "decodeR8G8B8A8Unorm"
    "summarizeSceneProbe"
    "meanAbsoluteSceneProbeDifference")
  string(FIND "${sceneProbeModel}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL scene-radiance probe regression: model is missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "GetRendererData"
    "cubemapRenderTargets"
    "kSceneRadianceCandidateSlot"
    "PS-t10"
    "RE::")
  string(FIND "${runtimeSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: runtime contains forbidden flat-layout access '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "validDiffuseSH"
    "classifyDiffuseSH"
    "DiffuseSHState"
    "std::array<std::array<float, 4>, 3>")
  string(FIND "${projectionModel}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: projection model is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "publishedUsable_"
    "publishUnavailable"
    "blackReadbacks_"
    "ambient integration remains fail-closed")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL usability-gate regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "TextureCube<float4> NativeEnvironment : register(t0)"
    "RWTexture2D<float4> ProjectedDiffuse : register(u0)"
    "SamplerState LinearSampler : register(s0)"
    "numthreads(AxisSampleCount, AxisSampleCount, 1)"
    "GroupMemoryBarrierWithGroupSync"
    "NativeEnvironment.SampleLevel")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: projection shader is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"Features/ibl/IblRuntime.h\""
    "ibl::Runtime::get().onDeviceCreated"
    "ibl::Runtime::get().onPixelShaderCreated"
    "ibl::Runtime::get().captureProbeForShader"
    "ibl::Runtime::get().onCaptureProbeDraw"
    "ibl::Runtime::get().onCaptureProbeDrawComplete"
    "beginActiveIblCaptureProbe"
    "completeActiveIblCaptureProbe"
    "qualificationSessionActive.load"
    "ReplacementShaderFamily::dFLightAmbient"
    "ibl::Runtime::get().onDFLightAmbientBind")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: D3D hook is missing '${required}'")
  endif()
endforeach()

string(FIND "${runtimeHeader}" "observe" foundObserveContract)
if(foundObserveContract EQUAL -1)
  message(FATAL_ERROR
    "IBL foundation regression: header no longer documents observe-only ownership")
endif()

string(FIND "${resourceSource}"
  "IDR_IBL_DIFFUSE_PROJECTION_CS RCDATA" foundResource)
if(foundResource EQUAL -1)
  message(FATAL_ERROR
    "IBL foundation regression: compute shader is not embedded")
endif()

file(READ "${IBL_PROJECTION_SHADER_ASSET}" shaderBytecode HEX)
string(SUBSTRING "${shaderBytecode}" 0 8 shaderMagic)
if(NOT shaderMagic STREQUAL "44584243")
  message(FATAL_ERROR
    "IBL foundation regression: compute shader asset is not DXBC")
endif()
