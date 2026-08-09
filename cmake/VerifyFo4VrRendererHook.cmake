if(NOT DEFINED RENDERER_HOOK_SOURCE OR
   NOT EXISTS "${RENDERER_HOOK_SOURCE}")
  message(FATAL_ERROR "RENDERER_HOOK_SOURCE is missing")
endif()

file(READ "${RENDERER_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "kBSDFLightShaderVtableRva = 0x030BF3C8"
    "kGeometrySetupSlot = 9"
    "kGeometrySetupFunctionRva = 0x0291DCA0"
    "kRenderPassGeometryLinkOffset = 0x38"
    "kGeometryLinkGeometryOffset = 0x0"
    "kGeometryPropertyOffset = 0xB8"
    "kPropertyEmissiveMultiplierOffset = 0x1BC"
    "kGeometrySetupSignature"
    "isReadableRange"
    "isPlausibleObjectPointer"
    "vtableCellOwned"
    "lastSourceEmissiveMultiplierBits"
    "calls.fetch_add(1, std::memory_order_release)"
    "calls.load(std::memory_order_acquire)"
    "recordStage(GeometryWalkStage::renderPass)"
    "recordStage(GeometryWalkStage::geometryLink)"
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
  "[[nodiscard]] bool readEmissiveMultiplier" walkStart)
string(FIND "${source}"
  "void __fastcall hookGeometrySetup" hookStart)
if(walkStart EQUAL -1 OR hookStart EQUAL -1 OR NOT walkStart LESS hookStart)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: geometry walk boundary is missing")
endif()
math(EXPR walkLength "${hookStart} - ${walkStart}")
string(SUBSTRING "${source}" ${walkStart} ${walkLength} walkSource)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "isReadableRange"
    "applyQueuedSettingsFor")
  string(FIND "${walkSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook hot-path regression: found '${forbidden}'")
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
    "kBSLightingShaderVtableRva = 0x030BBDB8"
    "kGeometrySetupFunctionRva = 0x028B6B70"
    "kPassGeometryOffset = 0x18"
    "kGeometryPropertyOffset = 0x178"
    "kPropertyEmissiveMultiplierOffset = 0x1B0"
    "kGeometrySetupSlot = 6"
    "kPropertyEmissiveMultiplierOffset = 0xC8"
    "kPropertyEmissiveMultiplierOffset = 0xB8"
    "applyQueuedSettingsFor")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: forbidden stale claim '${forbidden}'")
  endif()
endforeach()
