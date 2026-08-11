foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    EFFECT_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${EFFECT_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedEffectLinearLightingContracts.inl"
    "kEffectShaderContracts"
    "effectReplacementShaders_"
    "originalEffectShaders_"
    "matchingEffectShaderContractMask_"
    "ReplacementShaderFamily::effect"
    "effectReplacementBinds_.fetch_add"
    "validEffect")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Effect Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kEffectShaderContractCount = 332"
    "EffectContractMask matchingEffectShaderContractMask"
    "AtomicEffectContractMask matchingEffectShaderContractMask_"
    "matchingEffectShaderContractMask"
    "effectReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Effect Linear Lighting regression: interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"../LinearLighting/LinearLighting.hlsli\""
    "LinearLightingEffect(baseColor.xyz)"
    "LinearLightingEffect(EffectPropertyColor.xyz)"
    "(EFFECT_TECHNIQUE & 0x1)"
    "(EFFECT_TECHNIQUE & 0x4)"
    "(EFFECT_TECHNIQUE & 0x20)"
    "(EFFECT_TECHNIQUE & 0x40)"
    "(EFFECT_TECHNIQUE & 0x1080)"
    "(EFFECT_TECHNIQUE & 0x1000)"
    "(EFFECT_TECHNIQUE & 0x00002000)"
    "(EFFECT_TECHNIQUE & 0x00004000)"
    "EffectGrayscaleTexture.Sample("
    "EffectGrayscaleSampler,"
    "EffectUnusedPerMaterial.x"
    "(EFFECT_TECHNIQUE & 0x00100000)"
    "(EFFECT_TECHNIQUE & 0x00000400)"
    "(EFFECT_TECHNIQUE & 0x03000000)"
    "(EFFECT_TECHNIQUE & 0x00000010)"
    "(EFFECT_TECHNIQUE & 0x00200000)"
    "baseColor.w *= input.texCoord.z;"
    "baseColor.xyz *= input.texCoord.z;"
    "(EFFECT_TECHNIQUE & 0x08000000)"
    "(EFFECT_TECHNIQUE & 0x40000000)"
    "EffectUIMaskTechniqueData[rectangleIndex + 2]"
    "LinearLightingEffect(EffectUIMaskTechniqueData[18].xyz)"
    "EffectPointLightPositionX[eyeIndex]"
    "EffectPointLightColorToLinear"
    "effectLightingMult"
    "LinearLightingFog(input.fogParam.xyz)"
    "LinearLightingFogAlpha(input.fogParam.w)"
    "EffectAlphaTest.y - sampledAlpha"
    "lightColor *= otherEffectMult;"
    "LinearLightingEffectAlpha(alpha)"
    "LinearLightingEffectVertexColor(input.vertexColor)")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Effect Linear Lighting regression: shader is missing '${required}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Effect Linear Lighting regression: frame-only shader declares b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_EFFECT_[A-Z0-9_]+_PS RCDATA"
  effectResources "${resourceSource}")
list(LENGTH effectResources effectResourceCount)
if(NOT effectResourceCount EQUAL 332)
  message(FATAL_ERROR
    "Effect Linear Lighting regression: expected 332 embedded shaders")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::effect)"
  effectSelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::dFLightAmbient"
  effectSelectionEnd)
if(effectSelectionStart EQUAL -1 OR effectSelectionEnd EQUAL -1 OR
   NOT effectSelectionStart LESS effectSelectionEnd)
  message(FATAL_ERROR
    "Effect Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR effectSelectionLength
  "${effectSelectionEnd} - ${effectSelectionStart}")
string(SUBSTRING "${runtimeSource}" ${effectSelectionStart}
  ${effectSelectionLength} effectSelectionSource)
string(FIND "${effectSelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Effect Linear Lighting regression: selection requests geometry b8")
endif()
