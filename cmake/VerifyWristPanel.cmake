if(NOT DEFINED WRIST_PANEL_SOURCE OR NOT EXISTS "${WRIST_PANEL_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_SOURCE is missing")
endif()
if(NOT DEFINED WRIST_PANEL_POSE_SOURCE OR
    NOT EXISTS "${WRIST_PANEL_POSE_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_POSE_SOURCE is missing")
endif()
if(NOT DEFINED LINEAR_LIGHTING_RUNTIME_SOURCE OR
    NOT EXISTS "${LINEAR_LIGHTING_RUNTIME_SOURCE}")
  message(FATAL_ERROR "LINEAR_LIGHTING_RUNTIME_SOURCE is missing")
endif()
if(NOT DEFINED D3D11_HOOK_SOURCE OR
    NOT EXISTS "${D3D11_HOOK_SOURCE}")
  message(FATAL_ERROR "D3D11_HOOK_SOURCE is missing")
endif()

file(READ "${WRIST_PANEL_SOURCE}" source)
file(READ "${WRIST_PANEL_POSE_SOURCE}" poseSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${D3D11_HOOK_SOURCE}" d3dSource)

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
    "Linear Lighting geometry proof:"
    "maintainD3D11ShaderBindHook(\"GameDataReady\")"
    "maintainD3D11ShaderBindHook(\"GameSessionReady\")"
    "maintainD3D11ShaderBindHook(\"WristDomReady\")"
    "maintainD3D11ShaderBindHook(\"WristAction\")"
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
    "frameDataUploads_.fetch_add"
    "firstReplacementContractPlusOne_.compare_exchange_strong"
    "applyQueuedSettingsForRenderBoundary();"
    "appliedSettingsRevision_.store")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting telemetry regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${runtimeSource}"
  "ID3D11PixelShader* Runtime::selectPixelShader" selectStart)
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
    "deviceCreationImportOwned"
    "createPixelShaderCellOwned"
    "pixelShaderBindCellOwned"
    "d3d11_hook_repair::advance"
    "pixelShaderBindRepairs"
    "pixelShaderBindRepairFailures"
    "pixelShaderBindRecursions"
    "kDeviceContextVtableEntryCount = 115"
    "immediateContextVtableShadow"
    "deviceCaptured.compare_exchange_strong"
    "Ignored additional Fallout4VR D3D11 device creation"
    "InterlockedCompareExchangePointer"
    "modulePathForAddress"
    "Installed isolated D3D11 immediate-context vtable shadow"
    "Restored D3D11 immediate-context vtable shadow"
    "readPointerCell")
  string(FIND "${d3dSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 hook-ownership regression: missing '${required}'")
  endif()
endforeach()

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
