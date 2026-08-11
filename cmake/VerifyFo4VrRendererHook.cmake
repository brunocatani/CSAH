if(NOT DEFINED RENDERER_HOOK_SOURCE OR
   NOT EXISTS "${RENDERER_HOOK_SOURCE}")
  message(FATAL_ERROR "RENDERER_HOOK_SOURCE is missing")
endif()

file(READ "${RENDERER_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "kBSDFLightShaderVtableRva = 0x030BF3C8"
    "kTechniqueSetupSlot = 3"
    "kTechniqueSetupFunctionRva = 0x02922810"
    "kGeometrySetupSlot = 9"
    "kGeometrySetupFunctionRva = 0x0291DCA0"
    "kDFLightDescriptorOffset = 0x48"
    "kLightingStateAccessorRva = 0x027AEEB0"
    "kLightingStateRva = 0x068787F0"
    "kLightingStateEmissiveMultiplierOffset = 0x1BC"
    "kNativeScalarPowThunkRva = 0x029917A8"
    "kLocalAmbientTransformAccessorRva = 0x027ADD90"
    "kFallbackAmbientTransformAccessorRva = 0x027AE6F0"
    "PowCallsite{ 0x02922AB5, 0xFFE8BC36 }"
    "PowCallsite{ 0x02922ABF, 0xFFE8B2CC }"
    "PowCallsite{ 0x029232E9, 0x0006E4BA }"
    "PowCallsite{ 0x029232FD, 0x0006E4A6 }"
    "PowCallsite{ 0x02923311, 0x0006E492 }"
    "PowCallsite{ 0x0291E9E7, 0x00072DBC }"
    "PowCallsite{ 0x0291E9FB, 0x00072DA8 }"
    "PowCallsite{ 0x0291EA0F, 0x00072D94 }"
    "kTechniqueSetupSignature"
    "kGeometrySetupSignature"
    "kLightingStateAccessorSignature"
    "kNativeScalarPowThunkSignature"
    "kLocalAmbientAccessorSignature"
    "kFallbackAmbientAccessorSignature"
    "std::byte{ 0xFF }, std::byte{ 0x25 }, std::byte{ 0x3A }"
    "std::byte{ 0xB2 }, std::byte{ 0x2B }, std::byte{ 0x00 }"
    "originalCallsitesOwned(kDirectionalPowCallsites, nativePow)"
    "kAmbientTransformCallsites"
    "allocateReachablePage("
    "DFLightDescriptorScope descriptorScope(descriptor)"
    "hookDirectionalScalarPow"
    "hookFallbackAmbientTransform"
    "hookLocalAmbientTransform"
    "dFLightProducerCallsitesOwned"
    "directionalAmbientInputScale("
    "scaleDirectionalAmbientTransform("
    "producerOwnershipReady"
    "directionalEnergySampleClaimed"
    "directionalEnergySampleReady.store("
    "directionalEnergySampleStorage.source[index] = value"
    "publishDFLightProducerSettings("
    "completed.fetch_add(1, std::memory_order_release)"
    "isReadableRange"
    "resolvedLightingState != state"
    "vtableCellOwned"
    "lastSourceEmissiveMultiplierBits"
    "calls.fetch_add(1, std::memory_order_release)"
    "calls.load(std::memory_order_acquire)"
    "recordStage(GeometrySourceStage::lightingState)"
    "recordStage(GeometrySourceStage::emissiveMultiplier)"
    "originalTechniqueSetup("
    "techniqueCalls.fetch_add(1, std::memory_order_release)"
    "originalGeometrySetup(receiver, pass, compiledProgram)"
    "updateGeometryEmissive(")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "hookAmbientScalarPow"
    "kAmbientPowCallsites")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: stale ambient CPU producer '${forbidden}'")
  endif()
endforeach()

string(FIND "${source}"
  "[[nodiscard]] float runDFLightPowProducer" producerStart)
string(FIND "${source}"
  "[[nodiscard]] bool readEmissiveMultiplier" producerEnd)
if(producerStart EQUAL -1 OR producerEnd EQUAL -1 OR
   NOT producerStart LESS producerEnd)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: DFLight producer boundary is missing")
endif()
math(EXPR producerLength "${producerEnd} - ${producerStart}")
string(SUBSTRING "${source}" ${producerStart} ${producerLength}
  producerBody)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "VirtualProtect"
    "logging::"
    "std::vector"
    "std::mutex"
    "new "
    "malloc")
  string(FIND "${producerBody}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR DFLight producer hot-path regression: found '${forbidden}'")
  endif()
endforeach()

string(FIND "${source}"
  "[[nodiscard]] bool readEmissiveMultiplier" sourceStart)
string(FIND "${source}"
  "void __fastcall hookGeometrySetup" hookStart)
if(sourceStart EQUAL -1 OR hookStart EQUAL -1 OR
   NOT sourceStart LESS hookStart)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: geometry source boundary is missing")
endif()
math(EXPR sourceLength "${hookStart} - ${sourceStart}")
string(SUBSTRING "${source}" ${sourceStart} ${sourceLength}
  geometrySource)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "isReadableRange"
    "applyQueuedSettingsFor")
  string(FIND "${geometrySource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook hot-path regression: found '${forbidden}'")
  endif()
endforeach()

string(FIND "${source}"
  "originalGeometrySetup(receiver, pass, compiledProgram)" original_call)
string(FIND "${source}"
  "const auto validSource = readEmissiveMultiplier" source_read)
string(FIND "${source}" "updateGeometryEmissive(" update_call)
if(original_call EQUAL -1 OR source_read EQUAL -1 OR
   original_call GREATER source_read)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: emissive source must be sampled after the original geometry transaction")
endif()
if(original_call GREATER update_call)
  message(FATAL_ERROR
    "FO4VR renderer hook regression: b8 update must occur after the original geometry transaction")
endif()

foreach(forbidden IN ITEMS
    "kBSLightingShaderVtableRva = 0x030BBDB8"
    "kGeometrySetupFunctionRva = 0x028B6B70"
    "kRenderPassGeometryLinkOffset = 0x38"
    "kGeometryLinkGeometryOffset = 0x0"
    "kPassGeometryOffset = 0x18"
    "kGeometryPropertyOffset = 0x178"
    "kGeometryPropertyOffset = 0xB8"
    "kPropertyEmissiveMultiplierOffset = 0x1B0"
    "kPropertyEmissiveMultiplierOffset = 0x1BC"
    "kGeometrySetupSlot = 6"
    "kPropertyEmissiveMultiplierOffset = 0xC8"
    "kPropertyEmissiveMultiplierOffset = 0xB8"
    "isPlausibleObjectPointer"
    "applyQueuedSettingsFor")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "FO4VR renderer hook regression: forbidden stale claim '${forbidden}'")
  endif()
endforeach()
