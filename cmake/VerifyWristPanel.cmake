if(NOT DEFINED WRIST_PANEL_SOURCE OR NOT EXISTS "${WRIST_PANEL_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_SOURCE is missing")
endif()
if(NOT DEFINED WRIST_PANEL_POSE_SOURCE OR
    NOT EXISTS "${WRIST_PANEL_POSE_SOURCE}")
  message(FATAL_ERROR "WRIST_PANEL_POSE_SOURCE is missing")
endif()

file(READ "${WRIST_PANEL_SOURCE}" source)
file(READ "${WRIST_PANEL_POSE_SOURCE}" poseSource)

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
