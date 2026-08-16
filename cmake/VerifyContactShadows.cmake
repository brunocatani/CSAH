foreach(variable IN ITEMS
    CONTACT_SHADOW_RUNTIME_SOURCE
    CONTACT_SHADOW_RUNTIME_HEADER
    CONTACT_SHADOW_SETTINGS_HEADER
    CONTACT_SHADOW_SETTINGS_STORE_SOURCE
    CONTACT_SHADOW_SETTINGS_STORE_HEADER
    CONTACT_SHADOW_SHADER_SOURCE
    CONTACT_SHADOW_MASK_SHADER_SOURCE
    CONTACT_SHADOW_SHADER_GENERATOR
    D3D11_HOOK_SOURCE
    DEVMENU_MANIFEST_SOURCE
    PLUGIN_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${CONTACT_SHADOW_RUNTIME_SOURCE}" runtimeSource)
file(READ "${CONTACT_SHADOW_RUNTIME_HEADER}" runtimeHeader)
file(READ "${CONTACT_SHADOW_SETTINGS_HEADER}" settingsHeader)
file(READ "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}" settingsStoreSource)
file(READ "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}" settingsStoreHeader)
file(READ "${CONTACT_SHADOW_SHADER_SOURCE}" shaderSource)
file(READ "${CONTACT_SHADOW_MASK_SHADER_SOURCE}" maskShaderSource)
file(READ "${CONTACT_SHADOW_SHADER_GENERATOR}" generatorSource)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)
file(READ "${DEVMENU_MANIFEST_SOURCE}" devMenuManifest)
file(READ "${PLUGIN_SOURCE}" pluginSource)

foreach(required IN ITEMS
    "kConstantSlot = 13"
    "kMaskSlot = 46"
    "matchingContractIndex"
    "fo4vr_cs_contact_shadow_dflight_contracts"
    "kMaximumShaderContracts = 32"
    "kMaximumTrackedShaders = 128"
    "resourcesReady_.store(false"
    "resourcesReady_.store(true"
    "PSGetConstantBuffers("
    "PSSetConstantBuffers(kConstantSlot"
    "PSGetShaderResources("
    "PSSetShaderResources(kMaskSlot"
    "dispatchMask"
    "ScopedComputeState restore"
    "recordDrawFallback"
    "selectPixelShader"
    "tracksOriginal"
    "for (auto& replacement : replacements_)"
    "for (auto& original : originals_)"
    "original.shader.Reset()")
  string(FIND "${runtimeSource}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows runtime regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS "RE::" "REL::" "GetRendererData")
  string(FIND "${runtimeSource}${runtimeHeader}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows runtime contains unverified engine access '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> ContactShadowMask : register(t46)"
    "ContactShadowSettings : register(b13)"
    "ContactShadowMask.Load"
    "lerp("
    "rawVisibility")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows stereo shader regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> SceneDepth : register(t0)"
    "RWTexture2D<unorm float> ContactShadowMask : register(u0)"
    "NativeDFLight : register(b2)"
    "NativeStereo : register(b8)"
    "NativeCamera : register(b12)"
    "ContactShadowSettings : register(b13)"
    "const float3 towardLight = normalize(DFLight[eye + 1u].xyz)"
    "eyeFirstPixel"
    "eyeLastPixel"
    "sampleEyeUv <= 0.0f"
    "sampleEyeUv >= 1.0f"
    "(sampleDepth <= 0.01f) != (centerDepth <= 0.01f)"
    "ReconstructViewDepth"
    "const float viewDepth = abs(surface.z)"
    "1.0f - smoothstep(0.0f, fadeDistance, viewDepth)"
    "StableRayStride"
    "LoadCompatibleViewPosition"
    "SampleEdgeAwareViewPosition"
    "const float relativeDifference"
    "relativeDifference > kBilinearThreshold"
    "EstimateReceiverNormal"
    "ClosestSurfaceTangent"
    "const float normalTowardLight"
    "const float receiverPlaneBias"
    "viewDepth * 1.0e-6f"
    "const float orientedPlaneSeparation"
    "BlockerOcclusion"
    "const float entry = smoothstep"
    "const float exit = 1.0f - smoothstep"
    "const uint stableSampleFloor = min(sampleCount, 8u)"
    "const uint sampleSlot = (index * sampleStride) % sampleCount"
    "occlusion = max(occlusion, hit)"
    "occlusion * distanceScale"
    "index < 16u")
  string(FIND "${maskShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows mask shader regression: missing '${required}'")
  endif()
endforeach()

foreach(pair IN ITEMS
    "2:1" "3:2" "4:3" "5:3" "6:5"
    "7:4" "8:5" "9:5" "10:7" "11:6"
    "12:7" "13:7" "14:9" "15:8" "16:9")
  string(REPLACE ":" ";" pairValues "${pair}")
  list(GET pairValues 0 sampleCount)
  list(GET pairValues 1 sampleStride)
  string(REGEX MATCH
    "case ${sampleCount}u:[ \t\r\n]*return ${sampleStride}u;"
    stableStrideMatch "${maskShaderSource}")
  if(NOT stableStrideMatch)
    message(FATAL_ERROR
      "Contact Shadows stable stride ${sampleCount}:${sampleStride} changed")
  endif()

  set(gcdA ${sampleCount})
  set(gcdB ${sampleStride})
  while(NOT gcdB EQUAL 0)
    math(EXPR gcdRemainder "${gcdA} % ${gcdB}")
    set(gcdA ${gcdB})
    set(gcdB ${gcdRemainder})
  endwhile()
  if(NOT gcdA EQUAL 1)
    message(FATAL_ERROR
      "Contact Shadows stride ${sampleStride} does not permute ${sampleCount} taps")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "length(surface)"
    "-normalize(DFLight[eye + 1u].xyz)"
    "sampleCount = max((uint)scaled"
    "clamp(ContactParams0.w, 2.0f, 16.0f) * distanceScale"
    "1.0f - separation / thickness"
    "hit * sampleWeight"
    "sampleBudget"
    "LoadCompatibleViewDepth"
    "SampleEdgeAwareViewDepth"
    "float4 laneOcclusion"
    "supportConfidence"
    "ContactParams1.x * 0.25f"
    "viewDepth * 5.0e-4f")
  string(FIND "${maskShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows mask shader restored unstable tap movement '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS "PixelNoise" "discard")
  string(FIND "${shaderSource}${maskShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows stereo shader contains forbidden '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "EXPECTED_IDENTITY = (26152, \"12280787d2a5110c820f433751c84648\")"
    "EXPECTED_ALIAS_KEYS = {0x01200202, 0x01200282, 0x11200202}"
    "EXPECTED_COMPATIBLE_IDENTITIES = {"
    "directional_pixel_shaders"
    "structurally compatible directional DFLight inventory changed"
    "write_shader_family_header"
    "CONTACT_MASK_SLOT = 46"
    "contact-shadow template contains an early return"
    "contains an injected early return"
    "contact-shadow mask compute assembly changed"
    "multiply_rgb(1, visibility_scratch)"
    "multiply_rgb(0, visibility_scratch)")
  string(FIND "${generatorSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows generation regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "contact_shadows::Runtime::get().onDeviceCreated"
    "contact_shadows::Runtime::get().onPixelShaderCreated"
    "contactShadowRuntime.selectPixelShader("
    "contactShadowRuntime.tracksOriginal(shader)"
    "firstTrackedContactShaderBindLogged"
    "wrappedGrassFeatureActive"
    "contactShadowRuntime.compositorReady("
    "if (dflightCompositorActive && classInstanceCount == 0)"
    "issueDrawWithContactShadows(context"
    "activeContactShadowsEnabled"
    "wrappedRuntime.scopeDraw("
    "activeContactShadowBinding.original")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows D3D ownership regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${hookSource}"
  "dflightCompositorActive && !qualificationActive" qualificationSuppression)
if(NOT qualificationSuppression EQUAL -1)
  message(FATAL_ERROR
    "Contact Shadows exact compositor must not be suppressed during material qualification")
endif()

string(FIND "${hookSource}"
  "void STDMETHODCALLTYPE hookPSSetShader(" psSetShaderStart)
string(FIND "${hookSource}"
  "void STDMETHODCALLTYPE hookDrawIndexed(" psSetShaderEnd)
if(psSetShaderStart EQUAL -1 OR psSetShaderEnd EQUAL -1 OR
   NOT psSetShaderStart LESS psSetShaderEnd)
  message(FATAL_ERROR
    "Contact Shadows PSSetShader ownership boundary is unavailable")
endif()
math(EXPR psSetShaderLength "${psSetShaderEnd} - ${psSetShaderStart}")
string(SUBSTRING "${hookSource}" ${psSetShaderStart}
  ${psSetShaderLength} psSetShaderSource)
string(FIND "${psSetShaderSource}"
  "auto contactShadowSelection =" contactSelectionStart)
string(FIND "${psSetShaderSource}"
  "const auto selection =" linearSelectionStart)
string(FIND "${psSetShaderSource}"
  "!contactShadowSelection.binding &&" contactPriorityGate)
if(contactSelectionStart EQUAL -1 OR linearSelectionStart EQUAL -1 OR
   contactPriorityGate EQUAL -1 OR
   NOT contactSelectionStart LESS linearSelectionStart)
  message(FATAL_ERROR
    "Contact Shadows exact compositor must take priority over generic Linear Lighting selection")
endif()

string(REGEX MATCHALL
  "issueDrawWithContactShadows\\(context" drawScopes
  "${hookSource}")
list(LENGTH drawScopes drawScopeCount)
if(NOT drawScopeCount EQUAL 4)
  message(FATAL_ERROR
    "Contact Shadows must own one fail-closed scope in all four draw paths")
endif()

foreach(required IN ITEMS
    "bEnabled"
    "bFoveated"
    "fStrength"
    "fMaxDistance"
    "fFadeDistance"
    "fThickness"
    "iSampleCount"
    "parseBoolean")
  string(FIND
    "${settingsHeader}${settingsStoreSource}${settingsStoreHeader}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "\"id\": \"contact-shadows\""
    "\"id\": \"contact-foveated\""
    "\"section\": \"ContactShadows\""
    "\"key\": \"bEnabled\""
    "\"key\": \"bFoveated\""
    "\"key\": \"fStrength\""
    "\"key\": \"fMaxDistance\""
    "\"key\": \"fFadeDistance\""
    "\"key\": \"fThickness\""
    "\"key\": \"iSampleCount\"")
  string(FIND "${devMenuManifest}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows DevMenu regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "contact_shadows::loadSettings"
    "contact_shadows::Runtime::get().applySettings")
  string(FIND "${pluginSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows bootstrap regression: missing '${required}'")
  endif()
endforeach()
