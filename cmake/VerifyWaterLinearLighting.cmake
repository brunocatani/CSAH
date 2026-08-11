foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    WATER_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${WATER_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedWaterLinearLightingContracts.inl"
    "kWaterShaderContracts"
    "waterReplacementShaders_"
    "originalWaterShaders_"
    "matchingWaterShaderContractMask_"
    "ReplacementShaderFamily::water"
    "waterReplacementBinds_.fetch_add"
    "validWater")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Water Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kWaterShaderContractCount = 17"
    "matchingWaterShaderContractMask"
    "waterReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Water Linear Lighting regression: interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "float4 ShallowColor : packoffset(c0);"
    "float4 DeepColor : packoffset(c1);"
    "uint EnableLinearLighting : packoffset(c0.x);"
    "float WaterGamma : packoffset(c3.y);"
    "pow(abs(output.shallow.xyz), WaterGamma)"
    "pow(abs(output.deep.xyz), WaterGamma)")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Water Linear Lighting regression: transform template is missing '${required}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Water Linear Lighting regression: frame-only transform declares b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_WATER_[0-9A-F]+_PS RCDATA"
  waterResources "${resourceSource}")
list(LENGTH waterResources waterResourceCount)
if(NOT waterResourceCount EQUAL 17)
  message(FATAL_ERROR
    "Water Linear Lighting regression: expected seventeen embedded shaders")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::water)"
  waterSelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::effect)"
  waterSelectionEnd)
if(waterSelectionStart EQUAL -1 OR waterSelectionEnd EQUAL -1 OR
   NOT waterSelectionStart LESS waterSelectionEnd)
  message(FATAL_ERROR
    "Water Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR waterSelectionLength
  "${waterSelectionEnd} - ${waterSelectionStart}")
string(SUBSTRING "${runtimeSource}" ${waterSelectionStart}
  ${waterSelectionLength} waterSelectionSource)
string(FIND "${waterSelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Water Linear Lighting regression: selection requests geometry b8")
endif()
