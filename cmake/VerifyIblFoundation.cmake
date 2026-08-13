foreach(variable IN ITEMS
    IBL_RUNTIME_SOURCE
    IBL_RUNTIME_HEADER
    IBL_SETTINGS_HEADER
    IBL_SETTINGS_STORE_SOURCE
    IBL_SETTINGS_STORE_HEADER
    IBL_PROVIDER_MODEL
    IBL_ENVIRONMENT_PROVIDER_SOURCE
    IBL_ENVIRONMENT_PROVIDER_HEADER
    IBL_ENVIRONMENT_UPDATER_SOURCE
    IBL_ENVIRONMENT_UPDATER_HEADER
    IBL_COMPUTE_STATE_SCOPE_SOURCE
    IBL_COMPUTE_STATE_SCOPE_HEADER
    IBL_PROJECTION_MODEL
    IBL_CAPTURE_PROBE_MODEL
    IBL_SCENE_RADIANCE_PROBE_MODEL
    IBL_REFLECTION_FREE_CAPTURE_SOURCE
    IBL_REFLECTION_FREE_CAPTURE_HEADER
    IBL_MATERIAL_BINDING_SCOPE_SOURCE
    IBL_MATERIAL_BINDING_SCOPE_HEADER
    IBL_MATERIAL_GENERATED_CONTRACTS
    IBL_PROJECTION_SHADER_SOURCE
    IBL_PROJECTION_SHADER_ASSET
    IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE
    IBL_ENVIRONMENT_UPDATE_SHADER_ASSET
    IBL_ENVIRONMENT_FILTER_SHADER_SOURCE
    IBL_ENVIRONMENT_FILTER_SHADER_ASSET
    D3D11_HOOK_SOURCE
    GEOMETRY_HOOK_SOURCE
    WRIST_PANEL_SOURCE
    WRIST_PANEL_VIEW_SOURCE
    PLUGIN_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${IBL_RUNTIME_SOURCE}" runtimeSource)
file(READ "${IBL_RUNTIME_HEADER}" runtimeHeader)
file(READ "${IBL_SETTINGS_HEADER}" iblSettingsHeader)
file(READ "${IBL_SETTINGS_STORE_SOURCE}" iblSettingsStoreSource)
file(READ "${IBL_SETTINGS_STORE_HEADER}" iblSettingsStoreHeader)
file(READ "${IBL_PROVIDER_MODEL}" providerModel)
file(READ "${IBL_ENVIRONMENT_PROVIDER_SOURCE}" environmentProviderSource)
file(READ "${IBL_ENVIRONMENT_PROVIDER_HEADER}" environmentProviderHeader)
file(READ "${IBL_ENVIRONMENT_UPDATER_SOURCE}" environmentUpdaterSource)
file(READ "${IBL_ENVIRONMENT_UPDATER_HEADER}" environmentUpdaterHeader)
file(READ "${IBL_COMPUTE_STATE_SCOPE_SOURCE}" computeStateScopeSource)
file(READ "${IBL_COMPUTE_STATE_SCOPE_HEADER}" computeStateScopeHeader)
file(READ "${IBL_PROJECTION_MODEL}" projectionModel)
file(READ "${IBL_CAPTURE_PROBE_MODEL}" captureProbeModel)
file(READ "${IBL_SCENE_RADIANCE_PROBE_MODEL}" sceneProbeModel)
file(READ "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}" reflectionFreeCaptureSource)
file(READ "${IBL_REFLECTION_FREE_CAPTURE_HEADER}" reflectionFreeCaptureHeader)
file(READ "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}" materialBindingScopeSource)
file(READ "${IBL_MATERIAL_BINDING_SCOPE_HEADER}" materialBindingScopeHeader)
file(READ "${IBL_MATERIAL_GENERATED_CONTRACTS}" materialGeneratedContracts)
file(READ "${IBL_PROJECTION_SHADER_SOURCE}" shaderSource)
file(READ "${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}" updateShaderSource)
file(READ "${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}" filterShaderSource)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)
file(READ "${GEOMETRY_HOOK_SOURCE}" geometryHookSource)
file(READ "${WRIST_PANEL_SOURCE}" wristPanelSource)
file(READ "${WRIST_PANEL_VIEW_SOURCE}" wristPanelViewSource)
file(READ "${PLUGIN_SOURCE}" pluginSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "D3D11_MAP_FLAG_DO_NOT_WAIT"
    "CopyResource"
    "onDFLightAmbientBind"
    "environmentUpdater_.consumeUpdate"
    "update.diffuseSHCoverage"
    "publishUsable(update.diffuseSH"
    "classifyCaptureProbeShader"
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
    "selectMaterialPixelShader"
    "scopeMaterialBindings"
    "publishedEnvironment()"
    "publishedValidity()"
    "materialConsumptionFailed_"
    "createMaterialResources"
    "materialDisabledConstants_"
    "materialEnabledConstants_")
  string(FIND "${runtimeSource}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL material-consumption regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "std::atomic_bool enabled_{ true }"
    "std::atomic_bool diffuseEnabled_{ true }"
    "void Runtime::setEnabled(bool enabled) noexcept"
    "void Runtime::applySettings(const Settings& settings) noexcept"
    "tryGetDiffuseAmbient"
    "!enabled_.load(std::memory_order_acquire)"
    "publishedEnvironmentSessionId_"
    "kEnvironmentCaptureCadenceMilliseconds = 1000"
    "synchronizeWorldCaptureSession"
    "pendingEnvironmentUpdateSessionId_")
  string(FIND "${runtimeSource}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL runtime-control regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSection = L\"ImageBasedLighting\""
    "kEnabledKey = L\"bEnabled\""
    "kDiffuseEnabledKey = L\"bDiffuseEnabled\""
    "kDiffuseLevelKey = L\"fDiffuseLevel\""
    "GetPrivateProfileStringW"
    "WritePrivateProfileStringW"
    "parseBoolean"
    "settings_path::resolveIniPath()")
  string(FIND
    "${iblSettingsHeader}${iblSettingsStoreSource}${iblSettingsStoreHeader}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "type == \"iblEnabled\""
    "type == \"iblDiffuseEnabled\""
    "type == \"iblDiffuseLevel\""
    "ibl::Runtime::get().applySettings"
    "ibl::saveSettings"
    "\"ibl\"")
  string(FIND "${wristPanelSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL wrist-control regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "id=\"iblSwitch\""
    "id=\"iblDiffuseSwitch\""
    "Image Based Lighting"
    "Diffuse IBL"
    "type: \"iblEnabled\""
    "type: \"iblDiffuseEnabled\""
    "type: \"iblDiffuseLevel\""
    "toggleIblEnabled")
  string(FIND "${wristPanelViewSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL wrist-view regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kRadianceSlot = 30"
    "kValiditySlot = 31"
    "kConstantSlot = 5"
    "PSGetShaderResources"
    "PSSetShaderResources"
    "PSGetConstantBuffers"
    "PSSetConstantBuffers"
    "restoreCaptured()")
  string(FIND
    "${materialBindingScopeSource}${materialBindingScopeHeader}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL material-binding transaction regression: scope is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kIblMaterialShaderDefinitions"
    "IDR_IBL_MATERIAL_00_PS"
    "IDR_IBL_MATERIAL_40_PS")
  string(FIND "${materialGeneratedContracts}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL generated material contract regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "class EnvironmentUpdater"
    "dispatchUpdate"
    "consumeUpdate"
    "ScopedComputeState restore("
    "PSGetShaderResources(7, 1"
    "PSGetConstantBuffers(12, 1"
    "environmentUpdater_.dispatchUpdate"
    "environmentUpdater_.consumeUpdate"
    "provider.publishUpdate()"
    "CopySubresourceRegion"
    "D3D11_QUERY_EVENT"
    "D3D11_ASYNC_GETDATA_DONOTFLUSH")
  string(FIND
    "${runtimeSource}${environmentUpdaterSource}${environmentUpdaterHeader}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL environment-update regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "class EnvironmentUpdateCoverage"
    "environmentCubeDirection"
    "kEnvironmentCubeFaceCount = 6"
    "kEnvironmentMaximumMipCount = 10")
  string(FIND "${providerModel}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL provider-model regression: model is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float3> ReflectionFreeRadiance : register(t0)"
    "Texture2D<float> SceneDepth : register(t1)"
    "TextureCube<float3> PreviousEnvironment : register(t2)"
    "TextureCube<float> PreviousValidity : register(t3)"
    "RWTexture2DArray<float3> EnvironmentMip : register(u0)"
    "RWTexture2DArray<float> EnvironmentValidity : register(u1)"
    "EnvironmentUpdateConstants : register(b11)"
    "Fo4VrSceneConstants : register(b12)"
    "63u + eye * 4u"
    "HistoryAvailable"
    "HistoryDecay"
    "retainedValidity"
    "float2(0.5f, -0.5f)"
    "dispatchId.z >= 6u"
    "numthreads(8, 8, 1)")
  string(FIND "${updateShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL environment-update shader regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "TextureCube<float3> CapturedRadiance : register(t0)"
    "TextureCube<float> CapturedValidity : register(t1)"
    "RWTexture2DArray<float3> FilteredRadiance : register(u0)"
    "RWTexture2DArray<float> FilteredValidity : register(u1)"
    "EnvironmentFilterConstants : register(b11)"
    "SampleCount = 32u"
    "SampleGgx"
    "RadicalInverse"
    "saturate(validWeight / totalWeight)"
    "numthreads(8, 8, 1)")
  string(FIND "${filterShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL environment-filter regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "class EnvironmentProvider"
    "EnvironmentProviderState"
    "beginUpdate"
    "markSubresourceComplete"
    "publishUpdate"
    "abortUpdate"
    "publishedEnvironment"
    "publishedValidity"
    "publishedGeneration")
  string(FIND "${environmentProviderHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL provider regression: header is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "D3D11_RESOURCE_MISC_TEXTURECUBE"
    "D3D11_BIND_SHADER_RESOURCE |"
    "D3D11_BIND_UNORDERED_ACCESS"
    "D3D11_SRV_DIMENSION_TEXTURECUBE"
    "D3D11_UAV_DIMENSION_TEXTURE2DARRAY"
    "kValidityFormat"
    "chain.radiance"
    "chain.validity"
    "ResourceSet candidate"
    "frontChain_ = 1 - frontChain_"
    "coverage_.complete")
  string(FIND "${environmentProviderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL provider regression: source is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "ComputeStateFootprint"
    "CSGetShaderResources"
    "CSGetUnorderedAccessViews"
    "CSGetSamplers"
    "CSGetConstantBuffers"
    "CSSetShaderResources"
    "CSSetUnorderedAccessViews"
    "CSSetSamplers"
    "CSSetConstantBuffers"
    "restore() noexcept")
  string(FIND
    "${computeStateScopeHeader}${computeStateScopeSource}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL compute-state regression: scope is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "class ReflectionFreeCaptureResources"
    "class ScopedReflectionFreeCapture"
    "enum class ReflectionFreeCaptureRejection"
    "reflectionFreeCaptureRejectionName"
    "prepareScratch"
    "scratchMatches"
    "blackEnvironment"
    "blackScreenReflection"
    "restore() noexcept")
  string(FIND
    "${reflectionFreeCaptureHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL reflection-free capture regression: header is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kEnvironmentCubeCount = 42"
    "D3D11_SRV_DIMENSION_TEXTURECUBEARRAY"
    "D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE"
    "PSGetShaderResources"
    "PSSetShaderResources"
    "OMGetRenderTargets"
    "OMSetRenderTargetsAndUnorderedAccessViews"
    "D3D11_KEEP_UNORDERED_ACCESS_VIEWS"
    "OMGetBlendState"
    "RenderTargetWriteMask"
    "radianceWriteMask"
    "LogicOpEnable"
    "sampleMask & 1U"
    "SOGetTargets"
    "D3D11_DEPTH_WRITE_MASK_ZERO"
    "StencilWriteMask = 0"
    "appliedStateMatches"
    "(void)restore()")
  string(FIND
    "${reflectionFreeCaptureSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL reflection-free capture regression: source is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kWorldCaptureProbeSettleMilliseconds = 5000"
    "kSceneRadianceCandidateSlots"
    "activateWorldCaptureProbeSession"
    "kSceneRadianceMaximumReadbackPolls = 80"
    "CopySubresourceRegion"
    "createSceneRadianceProbeResources"
    "consumeSceneRadianceProbeReadbacks"
    "D3D11_MAP_FLAG_DO_NOT_WAIT"
    "rollingCompositeTexture"
    "rollingReflectionFreeTexture"
    "rollingReady"
    "beginReflectionFreeCapture"
    "onReflectionFreeCaptureDrawComplete"
    "reason={}, code={}"
    "onCaptureProbeDrawComplete"
    "IBL scene-radiance pass-end final-draw snapshot DFComposite"
    "IBL scene-radiance PS-t{}"
    "IBL scene-radiance OM-composite:"
    "IBL scene-radiance reflection-free split:"
    "onCaptureProbePassComplete")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL scene-radiance probe regression: runtime is missing '${required}'")
  endif()
endforeach()

string(FIND "${runtimeHeader}"
  "kCaptureShaderSlotCount = 256" foundCaptureRegistryCapacity)
if(foundCaptureRegistryCapacity EQUAL -1)
  message(FATAL_ERROR
    "IBL capture-probe regression: complete-family registry capacity changed")
endif()

string(FIND "${captureProbeModel}"
  "shouldCaptureCompletedProbePass" foundCapturePassTransition)
if(foundCapturePassTransition EQUAL -1)
  message(FATAL_ERROR
    "IBL capture-probe regression: pass-complete transition model is missing")
endif()

foreach(required IN ITEMS
    "kDFCompositeBoundaryContracts"
    "classifyCaptureProbeShader"
    "advanceCaptureProbePass")
  string(FIND "${captureProbeModel}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL capture-probe regression: complete-family model is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSceneProbeColumnCount = 16"
    "kSceneProbeRowCount = 4"
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
    "RE::")
  string(FIND "${runtimeSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: runtime contains forbidden flat-layout access '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "kSceneRadianceCandidateT5Slot"
    "kSceneRadianceCandidateT6Slot"
    "rollingPixelShaderT5Texture"
    "rollingPixelShaderT6Texture")
  string(FIND "${runtimeSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "IBL scene-radiance regression: obsolete sparse-probe path '${forbidden}' returned")
  endif()
endforeach()

foreach(required IN ITEMS
    "validDiffuseSH"
    "classifyDiffuseSH"
    "DiffuseSHState"
    "std::array<std::array<float, 4>, 3>"
    "accumulateDiffuseSHFit"
    "solveDiffuseSHFit"
    "evaluateDiffuseIrradiance"
    "buildDirectionalAmbientTransform")
  string(FIND "${projectionModel}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: projection model is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"Features/ibl/IblRuntime.h\""
    "beginWorldCaptureProbeSession")
  string(FIND "${pluginSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL world-session regression: plugin source is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "publishedUsable_"
    "publishUnavailable"
    "diffuseFitsPublished_"
    "kMinimumDiffuseSHCoverage")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL usability-gate regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"Features/ibl/IblRuntime.h\""
    "ibl::Runtime::get().tryGetDiffuseAmbient"
    "ibl::buildDirectionalAmbientTransform"
    "kVanillaDFLightGamma"
    "producerOwnershipReady.load(std::memory_order_acquire)")
  string(FIND "${geometryHookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL diffuse-hook regression: geometry hook is missing '${required}'")
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
    "ibl::Runtime::get().captureProbeBindingForShader"
    ".selectMaterialPixelShader(context, shader)"
    "activeIblMaterialBinding"
    "issueDrawWithIblMaterial"
    "scopeMaterialBindings("
    "activeIblMaterialBinding.original"
    "activeIblMaterialBinding.replacement"
    "ibl::Runtime::get().onCaptureProbeDraw"
    ".beginReflectionFreeCapture("
    ".onReflectionFreeCaptureDrawComplete("
    "ibl::Runtime::get().onCaptureProbeDrawComplete"
    "ibl::Runtime::get().onCaptureProbePassComplete"
    "recordActiveIblCaptureProbe"
    "preserveActiveIblCaptureProbeDraw"
    "completeIblCaptureProbePass"
    "ibl::shouldCaptureCompletedProbePass"
    "ibl::advanceCaptureProbePass"
    "if (activeIblCaptureProbePass.lastEnvironmentContractPlusOne == 0)"
    "if (!ibl::shouldCaptureCompletedProbePass("
    "ReplacementShaderFamily::dFLightAmbient"
    "ibl::Runtime::get().onDFLightAmbientBind")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "IBL foundation regression: D3D hook is missing '${required}'")
  endif()
endforeach()

string(FIND "${hookSource}"
  "qualificationSessionActive.load(std::memory_order_acquire) ||\n                activeIblCaptureProbePass"
  foundQualificationCoupling)
if(NOT foundQualificationCoupling EQUAL -1)
  message(FATAL_ERROR
    "IBL world-session regression: capture hooks were recoupled to Linear Lighting qualification")
endif()

string(FIND "${runtimeHeader}" "image-neutral" foundImageNeutralContract)
if(foundImageNeutralContract EQUAL -1)
  message(FATAL_ERROR
    "IBL foundation regression: header no longer documents image-neutral ownership")
endif()

string(FIND "${resourceSource}"
  "IDR_IBL_ENVIRONMENT_UPDATE_CS RCDATA" foundUpdateResource)
if(foundUpdateResource EQUAL -1)
  message(FATAL_ERROR
    "IBL environment-update regression: compute shader is not embedded")
endif()

string(FIND "${resourceSource}"
  "IDR_IBL_ENVIRONMENT_FILTER_CS RCDATA" foundFilterResource)
if(foundFilterResource EQUAL -1)
  message(FATAL_ERROR
    "IBL environment-filter regression: compute shader is not embedded")
endif()

file(READ "${IBL_PROJECTION_SHADER_ASSET}" shaderBytecode HEX)
string(SUBSTRING "${shaderBytecode}" 0 8 shaderMagic)
if(NOT shaderMagic STREQUAL "44584243")
  message(FATAL_ERROR
    "IBL foundation regression: compute shader asset is not DXBC")
endif()

file(READ "${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}" updateShaderBytecode HEX)
string(SUBSTRING "${updateShaderBytecode}" 0 8 updateShaderMagic)
if(NOT updateShaderMagic STREQUAL "44584243")
  message(FATAL_ERROR
    "IBL environment-update regression: compute shader asset is not DXBC")
endif()

file(READ "${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}" filterShaderBytecode HEX)
string(SUBSTRING "${filterShaderBytecode}" 0 8 filterShaderMagic)
if(NOT filterShaderMagic STREQUAL "44584243")
  message(FATAL_ERROR
    "IBL environment-filter regression: compute shader asset is not DXBC")
endif()
