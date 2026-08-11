foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedVLSCompositeLinearLightingContract.inl"
    "kVLSCompositeShaderContract"
    "vlsCompositeReplacementShaders_"
    "originalVLSCompositeShaders_"
    "matchingVLSCompositeShaderContractMask_"
    "ReplacementShaderFamily::vlsComposite"
    "vlsCompositeReplacementBinds_.fetch_add"
    "validVLSComposite")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "VLS composite Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kVLSCompositeShaderContractCount = 1"
    "matchingVLSCompositeShaderContractMask"
    "vlsCompositeReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "VLS composite Linear Lighting regression: interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"../LinearLighting/LinearLighting.hlsli\""
    "LinearLightingVolumetricLighting(power.xxx)"
    "register(b2)"
    "register(t0)"
    "register(s0)")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "VLS composite Linear Lighting regression: shader is missing '${required}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "VLS composite Linear Lighting regression: frame-only shader declares b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_VLS_COMPOSITE_PS RCDATA"
  vlsCompositeResources "${resourceSource}")
list(LENGTH vlsCompositeResources vlsCompositeResourceCount)
if(NOT vlsCompositeResourceCount EQUAL 1)
  message(FATAL_ERROR
    "VLS composite Linear Lighting regression: expected one embedded shader")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::vlsComposite)"
  vlsCompositeSelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::effect)"
  vlsCompositeSelectionEnd)
if(vlsCompositeSelectionStart EQUAL -1 OR vlsCompositeSelectionEnd EQUAL -1 OR
   NOT vlsCompositeSelectionStart LESS vlsCompositeSelectionEnd)
  message(FATAL_ERROR
    "VLS composite Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR vlsCompositeSelectionLength
  "${vlsCompositeSelectionEnd} - ${vlsCompositeSelectionStart}")
string(SUBSTRING "${runtimeSource}" ${vlsCompositeSelectionStart}
  ${vlsCompositeSelectionLength} vlsCompositeSelectionSource)
string(FIND "${vlsCompositeSelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "VLS composite Linear Lighting regression: selection requests geometry b8")
endif()
