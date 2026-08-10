foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE
    RESOURCE_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE}" shaderSource)
file(READ "${RESOURCE_SOURCE}" resourceSource)

foreach(required IN ITEMS
    "GeneratedParticleLinearLightingContracts.inl"
    "kParticleShaderContracts"
    "particleReplacementShaders_"
    "originalParticleShaders_"
    "matchingParticleShaderContractMask_"
    "ReplacementShaderFamily::particle"
    "particleReplacementBinds_.fetch_add"
    "validParticle")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Particle Linear Lighting regression: runtime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kParticleShaderContractCount = 4"
    "matchingParticleShaderContractMask"
    "particleReplacementBinds")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Particle Linear Lighting regression: interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "#include \"../LinearLighting/LinearLighting.hlsli\""
    "PARTICLE_TECHNIQUE == 1 || PARTICLE_TECHNIQUE == 3"
    "PARTICLE_TECHNIQUE == 2 || PARTICLE_TECHNIQUE == 3"
    "baseColor.xyz = LinearLightingDiffuse(baseColor.xyz) * ColorScale;")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Particle Linear Lighting regression: shader is missing '${required}'")
  endif()
endforeach()

string(FIND "${shaderSource}" "register(b8)" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Particle Linear Lighting regression: frame-only shader declares b8")
endif()

string(REGEX MATCHALL
  "IDR_LINEAR_LIGHTING_PARTICLE_[A-Z_]+_PS RCDATA"
  particleResources "${resourceSource}")
list(LENGTH particleResources particleResourceCount)
if(NOT particleResourceCount EQUAL 4)
  message(FATAL_ERROR
    "Particle Linear Lighting regression: expected four embedded shaders")
endif()

string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::particle)"
  particleSelectionStart)
string(FIND "${runtimeSource}"
  "if (binding.family == ReplacementShaderFamily::dFLightAmbient"
  particleSelectionEnd)
if(particleSelectionStart EQUAL -1 OR particleSelectionEnd EQUAL -1 OR
   NOT particleSelectionStart LESS particleSelectionEnd)
  message(FATAL_ERROR
    "Particle Linear Lighting regression: family selection boundary is missing")
endif()
math(EXPR particleSelectionLength
  "${particleSelectionEnd} - ${particleSelectionStart}")
string(SUBSTRING "${runtimeSource}" ${particleSelectionStart}
  ${particleSelectionLength} particleSelectionSource)
string(FIND "${particleSelectionSource}"
  "ReplacementPixelConstants_Geometry" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Particle Linear Lighting regression: selection requests geometry b8")
endif()
