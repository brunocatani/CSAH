if(NOT DEFINED RENDERER_HOOK_SOURCE OR
   NOT EXISTS "${RENDERER_HOOK_SOURCE}")
  message(FATAL_ERROR "RENDERER_HOOK_SOURCE is missing")
endif()

file(READ "${RENDERER_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "kBSLightingShaderVtableRva = 0x030BBDB8"
    "kGeometrySetupSlot = 9"
    "kGeometrySetupFunctionRva = 0x028B6B70"
    "kPassGeometryOffset = 0x18"
    "kGeometryPropertyOffset = 0x178"
    "kPropertyEmissiveMultiplierOffset = 0x1B0"
    "kGeometrySetupSignature"
    "isReadableRange"
    "recordStage(GeometryWalkStage::pass)"
    "recordStage(GeometryWalkStage::geometry)"
    "recordStage(GeometryWalkStage::property)"
    "recordStage(GeometryWalkStage::emissiveMultiplier)"
    "originalGeometrySetup(receiver, pass, compiledProgram)"
    "updateGeometryEmissive(")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${source}"
  "originalGeometrySetup(receiver, pass, compiledProgram)" original_call)
string(FIND "${source}" "updateGeometryEmissive(" update_call)
if(original_call GREATER update_call)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: b8 update must occur after the original geometry transaction")
endif()

foreach(forbidden IN ITEMS
    "kGeometrySetupSlot = 6"
    "kPropertyEmissiveMultiplierOffset = 0xC8"
    "kPropertyEmissiveMultiplierOffset = 0xB8")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: forbidden stale claim '${forbidden}'")
  endif()
endforeach()
