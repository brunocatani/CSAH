foreach(variable IN ITEMS
    PLUGIN_SOURCE
    D3D11_HOOK_SOURCE
    QUALIFICATION_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${PLUGIN_SOURCE}" pluginSource)
file(READ "${D3D11_HOOK_SOURCE}" d3dSource)
file(READ "${QUALIFICATION_SOURCE}" qualificationSource)

foreach(required IN ITEMS
    "startLinearLightingQualificationReporter"
    "beginLinearLightingQualificationSession(\"PostLoadGame\")"
    "beginLinearLightingQualificationSession(\"NewGame\")")
  string(FIND "${pluginSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting qualification regression: plugin lifecycle is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kDrawIndexedVtableIndex = 12"
    "kDrawVtableIndex = 13"
    "kDrawIndexedInstancedVtableIndex = 20"
    "kDrawInstancedVtableIndex = 21"
    "activatePendingQualificationSession"
    "recordQualificationBinding"
    "recordQualificationDraw"
    "inspectReplacementPipelineState"
    "AtomicContractMask"
    "qualificationDrawDetoursOwned")
  string(FIND "${d3dSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting qualification regression: render proof is missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "std::ofstream"
    "MoveFileExW"
    "nlohmann::json")
  string(FIND "${d3dSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting qualification hot-path regression: found '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "std::jthread"
    "kQualificationTimeoutMilliseconds = 20'000"
    "FO4VRCommunityShaders.LinearLightingQualification.json"
    "waiting_for_world"
    "GetCurrentProcessId()"
    "MoveFileExW"
    "MOVEFILE_REPLACE_EXISTING"
    "capture.sample.sessionActivated"
    "{ \"schemaVersion\", 12 }"
    "verifiedSkyShaderContracts"
    "matchingSkyShaderContractMask"
    "matchingSkyShadersCreated"
    "trackedOriginalSkyShaders"
    "skyReplacementBinds"
    "verifiedDistantTreeShaderContracts"
    "matchingDistantTreeShaderContractMask"
    "matchingDistantTreeShadersCreated"
    "trackedOriginalDistantTreeShaders"
    "distantTreeReplacementBinds"
    "verifiedParticleShaderContracts"
    "matchingParticleShaderContractMask"
    "matchingParticleShadersCreated"
    "trackedOriginalParticleShaders"
    "particleReplacementBinds"
    "verifiedWaterShaderContracts"
    "matchingWaterShaderContractMask"
    "matchingWaterShadersCreated"
    "trackedOriginalWaterShaders"
    "waterReplacementBinds"
    "verifiedVLSCompositeShaderContracts"
    "matchingVLSCompositeShaderContractMask"
    "matchingVLSCompositeShadersCreated"
    "trackedOriginalVLSCompositeShaders"
    "vlsCompositeReplacementBinds"
    "verifiedEffectShaderContracts"
    "matchingEffectShaderContractMaskWords"
    "matchingEffectShadersCreated"
    "trackedOriginalEffectShaders"
    "effectReplacementBinds"
    "shaderBindingLookupFailures"
    "point_light_hook_unowned"
    "dflight_producer_callsites_unowned"
    "no_ambient_producer_proof"
    "no_directional_producer_proof"
    "dflight_invalid_pow_result"
    "validateBSLightingGeometryHook(\"Qualification\")"
    "dFLightProducerCallsitesOwned"
    "ambientTransformPrepared"
    "ambientShaderReplacementBinds"
    "dFLightAmbientReplacementBinds"
    "matchingDFLightAmbientContractMask"
    "readyDFLightAmbientContractMask"
    "directionalPowModified"
    "dFLightInvalidPowResults"
    "pointLightDetourOwned"
    "pointLightGammaLoadsOwned"
    "pointLightModified"
    "least-significant-first"
    "effectContractMaskEncoding"
    "matchingShaderContractMaskWords"
    "matchingEffectShaderContractMaskWords"
    "fullyVerifiedContractMaskWords"
    "publishPublicStatus"
    "publishArmedSession"
    "LinearLightingQualificationState::waitingForWorld"
    "LinearLightingQualificationState::running"
    "LinearLightingQualificationState::passed"
    "LinearLightingQualificationState::failed"
    "publicFullyVerifiedContracts"
    "linearLightingQualificationSnapshot")
  string(FIND "${qualificationSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting qualification regression: reporter is missing '${required}'")
  endif()
endforeach()
