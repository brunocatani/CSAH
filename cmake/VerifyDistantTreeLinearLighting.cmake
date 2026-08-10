foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedDistantTreeLinearLightingContract.inl"
    "kDistantTreeShaderContract"
    "distantTreeReplacementShaders_"
    "originalDistantTreeShaders_"
    "matchingDistantTreeShaderContractMask_"
    "ReplacementShaderFamily::distantTree"
    "distantTreeReplacementBinds_.fetch_add"
    "validDistantTree")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DistantTree Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kDistantTreeShaderContractCount = 1"
    "matchingDistantTreeShaderContractMask"
    "distantTreeReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DistantTree Linear Lighting regression: interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"../LinearLighting/LinearLighting.hlsli\""
    "LinearLightingDiffuse(sampledDiffuse)"
    "LinearLightingDirectionalLight("
    "LinearLightingAmbient("
    "LinearLightingFog(input.fog.xyz)"
    "LinearLightingFogAlpha(input.fog.w)"
    "LinearLightingVanillaNormalization()")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DistantTree Linear Lighting regression: shader is missing '${required}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "DistantTree Linear Lighting regression: frame-only shader declares b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_DISTANT_TREE_BLOCK_PS RCDATA"
  distantTreeResources "${resourceSource}")
list(LENGTH distantTreeResources distantTreeResourceCount)
if(NOT distantTreeResourceCount EQUAL 1)
  message(FATAL_ERROR
    "DistantTree Linear Lighting regression: expected one embedded shader")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::distantTree)"
  distantTreeSelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::dFLightAmbient"
  distantTreeSelectionEnd)
if(distantTreeSelectionStart EQUAL -1 OR distantTreeSelectionEnd EQUAL -1 OR
   NOT distantTreeSelectionStart LESS distantTreeSelectionEnd)
  message(FATAL_ERROR
    "DistantTree Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR distantTreeSelectionLength
  "${distantTreeSelectionEnd} - ${distantTreeSelectionStart}")
string(SUBSTRING "${runtimeSource}" ${distantTreeSelectionStart}
  ${distantTreeSelectionLength} distantTreeSelectionSource)
string(FIND "${distantTreeSelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "DistantTree Linear Lighting regression: selection requests geometry b8")
endif()
