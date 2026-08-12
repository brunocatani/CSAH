if(NOT DEFINED POINT_LIGHT_HOOK_SOURCE OR
   NOT EXISTS "${POINT_LIGHT_HOOK_SOURCE}")
  message(FATAL_ERROR "POINT_LIGHT_HOOK_SOURCE is missing")
endif()

file(READ "${POINT_LIGHT_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "kPointLightProducerCallsiteRva = 0x027EE9AB"
    "kPointLightRecordConstructorRva = 0x02889940"
    "kVanillaGammaRva = 0x02C96CE4"
    "0x027EE8B6"
    "0x027EE8ED"
    "0x027EE907"
    "kProducerCallsiteSignature"
    "kPointLightRecordSignature"
    "kGammaLoadOpcode"
    "resolveRelativeTarget(callsite, 1, 5) != recordTarget"
    "resolveRelativeTarget("
    "allocateExponentStorage("
    "displacementForTarget("
    "captureMinHookPatchIdentity(recordTarget, patch)"
    "originalPointLightRecord("
    "&scaledColor"
    "hookOwnershipReady"
    "synchronizeExponentStorage()"
    "kVanillaPointLightGamma"
    "completedCalls.fetch_add(1, std::memory_order_release)")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DFTiled point-light hook regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "REL::"
    "kVanillaGammaRva = 0x02C96CE4;\n        *"
    "std::vector"
    "std::mutex"
    "pointLightEnergySample"
    "pow("
    "powf(")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "DFTiled point-light hook regression: forbidden '${forbidden}'")
  endif()
endforeach()

string(FIND "${source}" "void __fastcall hookPointLightRecord" hookStart)
string(FIND "${source}" "bool installDFTiledPointLightHook" installStart)
if(hookStart EQUAL -1 OR installStart EQUAL -1 OR
   NOT hookStart LESS installStart)
  message(FATAL_ERROR
    "DFTiled point-light hook regression: callback boundary is missing")
endif()
math(EXPR hookLength "${installStart} - ${hookStart}")
string(SUBSTRING "${source}" ${hookStart} ${hookLength} hookBody)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "VirtualProtect"
    "logging::"
    "new "
    "malloc"
    "std::scoped_lock")
  string(FIND "${hookBody}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "DFTiled point-light hot-path regression: found '${forbidden}'")
  endif()
endforeach()
