if(NOT DEFINED WRIST_PANEL_SOURCE OR NOT EXISTS "${WRIST_PANEL_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_SOURCE is missing")
endif()
if(NOT DEFINED WRIST_PANEL_POSE_SOURCE OR
    NOT EXISTS "${WRIST_PANEL_POSE_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_POSE_SOURCE is missing")
endif()
if(NOT DEFINED WRIST_PANEL_SETTINGS_SOURCE OR
    NOT EXISTS "${WRIST_PANEL_SETTINGS_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_SETTINGS_SOURCE is missing")
endif()
if(NOT DEFINED SETTINGS_PATH_SOURCE OR
    NOT EXISTS "${SETTINGS_PATH_SOURCE}")
  message(FATAL_ERROR "SETTINGS_PATH_SOURCE is missing")
endif()
if(NOT DEFINED LINEAR_LIGHTING_RUNTIME_SOURCE OR
    NOT EXISTS "${LINEAR_LIGHTING_RUNTIME_SOURCE}")
  message(FATAL_ERROR "LINEAR_LIGHTING_RUNTIME_SOURCE is missing")
endif()
if(NOT DEFINED WRIST_PANEL_VIEW_SOURCE OR
    NOT EXISTS "${WRIST_PANEL_VIEW_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_VIEW_SOURCE is missing")
endif()
file(READ "${WRIST_PANEL_SOURCE}" source)
file(READ "${WRIST_PANEL_POSE_SOURCE}" poseSource)
file(READ "${WRIST_PANEL_SETTINGS_SOURCE}" settingsSource)
file(READ "${SETTINGS_PATH_SOURCE}" settingsPathSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${WRIST_PANEL_VIEW_SOURCE}" viewSource)

foreach(required IN ITEMS
    "NetworkAccessPolicy::LocalOnly"
    "SpatialPresentationMode::WorldQuad"
    "SpatialUpdate_SceneDepthOcclusion"
    "SpatialFeature_CentralPointerRouting"
    "SpatialPointerSource_PhysicalLeftController"
    "ROCK_PROVIDER_API_V1_OWNER_FRAME_CALLBACKS_TABLE_BYTES"
    "registerFrameCallbackForOwnerV1"
    "setHandInputSuppressionV1"
    "clearHandInputSuppressionV1"
    "publishDebugOverlayV1"
    "wrist_panel_pose::composePanelOrientation"
    "wrist_panel_pose::kProberPanelPose.positionX"
    "wrist_panel_pose::kProberPanelPose.positionY"
    "wrist_panel_pose::kProberPanelPose.positionZ"
    "logLinearLightingMilestones"
    "Linear Lighting runtime activation proof:"
    "Linear Lighting D3D bind-hook proof:"
    "Linear Lighting first replacement proof:"
    "Linear Lighting first Sky replacement proof:"
    "Linear Lighting first DistantTree replacement proof:"
    "Linear Lighting first Particle replacement proof:"
    "Linear Lighting first VLS composite replacement proof:"
    "Linear Lighting first Effect replacement proof:"
    "Linear Lighting geometry-call proof:"
    "Linear Lighting geometry proof:"
    "validateD3D11ShaderHooks(\"GameDataReady\")"
    "validateD3D11ShaderHooks(\"GameSessionReady\")"
    "validateD3D11ShaderHooks(\"WristDomReady\")"
    "validateD3D11ShaderHooks(\"WristAction\")"
    "diagnostics::linearLightingQualificationSnapshot()"
    "linearLightingQualificationStateName"
    "FO4VR Linear Lighting shader coverage"
    "dfPrepassCoverageComplete"
    "skyCoverageComplete"
    "distantTreeCoverageComplete"
    "particleCoverageComplete"
    "vlsCompositeCoverageComplete"
    "effectCoverageComplete"
    "matchingEffectShaderMaskWords"
    "matchingWaterShaderMask"
    "waterReplacementBinds"
    "vlsCompositeReplacementBinds"
    "shaderBindingLookupFailures"
    "std::atomic_bool diagnosticsEnabled{ true }"
    "std::atomic_bool prismaPanelEnabled"
    "refreshPrismaPanelSetting(\"GameDataReady\")"
    "refreshPrismaPanelSetting(\"GameSessionReady\")"
    "wrist_panel_settings::load(path)"
    "shader runtime and ROCK diagnostics are independent"
    "scene %s | qualification %s | proof %u/%u"
    "wrist_provider_retry::Gate"
    "PrismaProbeFailure::SceneDepthPending"
    "attemptPrismaInitialization(\"GameDataReady\")"
    "attemptPrismaInitialization(\"GameSessionReady\")"
    "F4SE::GetTaskInterface()"
    "tasks->Version() < F4SE::TaskInterface::kVersion"
    "Runtime::get().queueSettings(next)")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel source regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "type == \"iblEnabled\""
    "type == \"iblDiffuseEnabled\""
    "type == \"iblDiffuseLevel\""
    "ibl::Runtime::get().applySettings"
    "ibl::saveSettings"
    "iblRuntime.resourcesReady")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel IBL control regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "SHGetFolderPathW"
    "CSIDL_MYDOCUMENTS"
    "L\"My Games\""
    "L\"Fallout4VR\""
    "L\"FO4VRCommunityShaders_Config\""
    "L\"FO4VRCommunityShaders.ini\""
    "std::filesystem::create_directories")
  string(FIND "${settingsPathSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Settings-path regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "GetModuleFileNameW"
    "Data\\F4SE\\Plugins")
  string(FIND "${settingsPathSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Settings-path regression: forbidden fallback '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSection = L\"PrismaPanel\""
    "kEnabledKey = L\"bEnabled\""
    "GetPrivateProfileStringW"
    "equalsIgnoreAsciiCase"
    "std::filesystem::is_regular_file"
    "result.valueValid = false")
  string(FIND "${settingsSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "FULL LINEAR LIGHTING ARMED"
    "id=\"featuresPage\""
    "id=\"responsePage\""
    "id=\"pageFeaturesButton\""
    "id=\"pageResponseButton\""
    "function setPage(page)"
    "ALL SWITCHES · IMMEDIATE SAVE"
    "FO4VRCommunityShaders_Config\\FO4VRCommunityShaders.ini"
    "Image Based Lighting"
    "id=\"iblSwitch\""
    "Diffuse IBL"
    "id=\"iblDiffuseSwitch\""
    "toggleIblEnabled"
    "toggleIblDiffuseEnabled"
    "type: \"iblEnabled\""
    "type: \"iblDiffuseEnabled\""
    "type: \"iblDiffuseLevel\""
    "const qualification = model.qualification || {}"
    "Runtime qualification:"
    "metric(\"Qualification\""
    "metric(\"Observed proof\""
    "metric(\"World lifecycle\""
    "metric(\"Render session\""
    "metric(\"DistantTree binds\""
    "metric(\"Particle binds\""
    "metric(\"VLS composite binds\""
    "metric(\"Effect binds\""
    "metric(\"Binding lookup\""
    "bounded, leased, automatic start"
    "QUAL \${qualificationLabel}")
  string(FIND "${viewSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel view regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "frameDataUploads_.fetch_add"
    "firstReplacementContractPlusOne_.compare_exchange_strong"
    "return reject(geometryResourceRejects_)"
    "return reject(geometryDisabledRejects_)"
    "return reject(geometryInvalidSourceRejects_)"
    "applyQueuedSettingsForRenderBoundary();"
    "appliedSettingsRevision_.store")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting telemetry regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "replacementCurrentlyBound_"
    "geometryUnboundRejects_")
  string(FIND "${runtimeSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting ordering regression: forbidden '${forbidden}'")
  endif()
endforeach()

string(FIND "${runtimeSource}"
  "PixelShaderSelection Runtime::selectPixelShader" selectStart)
string(FIND "${runtimeSource}"
  "void Runtime::queueSettings" queueStart)
if(selectStart EQUAL -1 OR queueStart EQUAL -1 OR
   NOT selectStart LESS queueStart)
  message(FATAL_ERROR
    "Linear Lighting telemetry regression: shader-selection boundary is missing")
endif()
math(EXPR selectLength "${queueStart} - ${selectStart}")
string(SUBSTRING "${runtimeSource}" ${selectStart} ${selectLength}
  selectSource)
string(FIND "${selectSource}"
  "applyQueuedSettingsForRenderBoundary();" boundaryApply)
if(boundaryApply EQUAL -1)
  message(FATAL_ERROR
    "Linear Lighting settings must be consumed by shader selection")
endif()

foreach(required IN ITEMS
    ".positionX = 7.75f"
    ".positionY = 9.0f"
    ".positionZ = -16.5f"
    ".rotationXDegrees = 0.0f"
    ".rotationYDegrees = 90.0f"
    ".rotationZDegrees = 6.0f"
    ".flipX = true"
    ".flipY = true"
    ".flipZ = false"
    "composePanelOrientation"
    "multiplyQuaternions(zRotation, *yThenX)")
  string(FIND "${poseSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel pose regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "ROCK_Configurator"
    "Runtime::get().applySettings(next)"
    "NetworkAccessPolicy::Unrestricted")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Wrist-panel source regression: forbidden '${forbidden}'")
  endif()
endforeach()
