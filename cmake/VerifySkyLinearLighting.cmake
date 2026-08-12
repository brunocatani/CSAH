foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    D3D11_HOOK_SOURCE
    SKY_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)
file(READ "${SKY_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedSkyLinearLightingContracts.inl"
    "skyReplacementShaders_"
    "originalSkyShaders_"
    "matchingSkyShaderContractMask_"
    "ReplacementShaderFamily::sky"
    "ReplacementPixelConstants_Frame"
    "validSky"
    "skyReplacementBinds_.fetch_add")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Sky Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kSkyShaderContractCount = 8"
    "std::uint16_t matchingSkyShaderContractMask"
    "std::uint64_t skyReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Sky Linear Lighting regression: runtime interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "activeReplacementBinding = selection.binding"
    "scopeReplacementPixelConstants("
    "activeReplacementBinding")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Sky Linear Lighting regression: draw binding is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"../LinearLighting/LinearLighting.hlsli\""
    "LinearLightingSky(baseColor.xyz)"
    "LinearLightingSkyProducerColor(input.color.xyz)"
    "color *= skyParameters.y;"
    "output.motion = ComputeMotionVector(input);")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Sky Linear Lighting regression: shader is missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "LinearLightingSky(input.color.xyz)"
    "LinearLightingSky(skyParameters.yyy)")
  string(FIND "${shaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Sky Linear Lighting regression: shader contains forbidden color-domain path '${forbidden}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Sky Linear Lighting regression: frame-only shader declares geometry b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_SKY_TECHNIQUE_[1-8]_PS RCDATA"
  skyResources "${resourceSource}")
list(LENGTH skyResources skyResourceCount)
if(NOT skyResourceCount EQUAL 8)
  message(FATAL_ERROR
    "Sky Linear Lighting regression: expected exactly eight embedded shaders")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::sky)" skySelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::distantTree)" skySelectionEnd)
if(skySelectionStart EQUAL -1 OR skySelectionEnd EQUAL -1 OR
   NOT skySelectionStart LESS skySelectionEnd)
  message(FATAL_ERROR
    "Sky Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR skySelectionLength "${skySelectionEnd} - ${skySelectionStart}")
string(SUBSTRING "${runtimeSource}" ${skySelectionStart}
  ${skySelectionLength} skySelectionSource)
string(FIND "${skySelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Sky Linear Lighting regression: Sky selection requests geometry b8")
endif()
