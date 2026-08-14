if(NOT DEFINED SURFACE_CLASSIFICATION_RUNTIME_SOURCE OR
   NOT EXISTS "${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}")
  message(FATAL_ERROR "SURFACE_CLASSIFICATION_RUNTIME_SOURCE is missing")
endif()
if(NOT DEFINED SURFACE_CLASSIFICATION_GENERATOR OR
   NOT EXISTS "${SURFACE_CLASSIFICATION_GENERATOR}")
  message(FATAL_ERROR "SURFACE_CLASSIFICATION_GENERATOR is missing")
endif()
if(NOT DEFINED SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE OR
   NOT EXISTS "${SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE}")
  message(FATAL_ERROR "SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE is missing")
endif()

file(READ "${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}" source)
file(READ "${SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE}" linear_source)
file(READ "${SURFACE_CLASSIFICATION_GENERATOR}" generator)

foreach(required IN ITEMS
    "kSurfaceClassMaterialContracts[contractIndex]"
    "materialClassUnambiguous"
    "selectedSurfaceClassCode"
    "ProducerEvidence::"
    "materialIdentity"
    "onVertexShaderCreated("
    "kGrassVertexShaderIdentities"
    "grassVertexShaderLookup_"
    "selectPixelShaderForGrassVertex("
    "grassVariantIndexPlusOne")
  string(FIND "${linear_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Surface-classification identity-path regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "DXGI_FORMAT_R16G16_FLOAT"
    "description.Width <= description.Height"
    "index < static_cast<UINT>(kFO4VRGBufferFormats.size())"
    "kFO4VRGBufferFormats.size(),"
    "binding.renderTargets[6] = renderTargetView_.Get()"
    "binding.renderTargetCount ="
    "markWorldFrameConsumed()"
    "if (worldFrameConsumed_)"
    "setDFPrePassSurfaceClassificationEnabled(next != 0)"
    "recordProducerSelection("
    "recordDescriptorObservation("
    "recordDescriptorObservationMiss("
    "recordDescriptorScopeMiss()"
    "recordDescriptorContractMiss(")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Surface-classification runtime regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "EXPECTED_SPECIALIZED_VARIANTS = 106"
    "EXPECTED_GRASS_VERTEX_SHADER_ROWS = 11"
    "EXPECTED_GRASS_VERTEX_SHADER_IDENTITIES = 5"
    "EXPECTED_AMBIGUOUS_MATERIAL_CONTRACTS = 6"
    "SURFACE_CLASS_AMBIGUOUS = 0xFFFFFFFF"
    "observed_unambiguous != EXPECTED_UNAMBIGUOUS_MATERIAL_CLASSES"
    "class_codes != {SURFACE_CLASS_ORDINARY, SURFACE_CLASS_GRASS}"
    "grass vertex-shader identity aliases a non-grass shader"
    "if descriptor & (1 << 7):"
    "return SURFACE_CLASS_GRASS"
    "expected 106 specialized surface-class identities")
  string(FIND "${generator}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Surface-classification generator regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${generator}" "EXPECTED_SPECIALIZED_VARIANTS = 100" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Surface-classification generator restored the pre-grass 100-variant contract")
endif()

foreach(forbidden IN ITEMS
    "DXGI_FORMAT_R11G11B10_FLOAT"
    "observeNonGBufferBinding"
    "gBufferActive_"
    "index < renderTargetCount; ++index"
    "renderTargets,\n            renderTargetCount,")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Surface-classification runtime restored invalid FO4VR G-buffer contract '${forbidden}'")
  endif()
endforeach()

string(FIND "${source}"
  "exact double-wide stereo six-target FO4VR G-buffer" found)
if(found EQUAL -1)
  message(FATAL_ERROR
    "Surface-classification runtime no longer identifies its accepted target as the FO4VR stereo world G-buffer")
endif()
