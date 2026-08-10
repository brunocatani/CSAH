foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    FIXED_SHADER_BINDING_LOOKUP_HEADER)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${FIXED_SHADER_BINDING_LOOKUP_HEADER}" lookupHeader)

foreach(required IN ITEMS
    "FixedShaderBindingLookup<kShaderBindingLookupCapacity>"
    "shaderBindingLookup_.find(requested)"
    "registerShaderBinding("
    "shaderBindingLookupFailures_")
  string(FIND "${runtimeSource}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Fixed shader-binding lookup regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "class FixedShaderBindingLookup final"
    "static_assert(std::has_single_bit(Capacity))"
    "std::atomic<const void*> shader"
    "std::atomic_uint64_t encodedBinding"
    "std::memory_order_release"
    "std::memory_order_acquire"
    "for (std::size_t probe = 0; probe < Capacity; ++probe)")
  string(FIND "${lookupHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Fixed shader-binding lookup regression: header is missing '${required}'")
  endif()
endforeach()

string(FIND "${runtimeSource}"
  "PixelShaderSelection Runtime::selectPixelShader(" selectionStart)
string(FIND "${runtimeSource}"
  "ScopedReplacementPixelConstants Runtime::scopeReplacementPixelConstants("
  selectionEnd)
if(selectionStart EQUAL -1 OR selectionEnd EQUAL -1 OR
   NOT selectionStart LESS selectionEnd)
  message(FATAL_ERROR
    "Fixed shader-binding lookup regression: selection boundary is missing")
endif()
math(EXPR selectionLength "${selectionEnd} - ${selectionStart}")
string(SUBSTRING "${runtimeSource}" ${selectionStart}
  ${selectionLength} selectionSource)
foreach(forbidden IN ITEMS
    "originalShaders_"
    "originalSkyShaders_"
    "originalDistantTreeShaders_"
    "dFLightAmbientOriginalShaders_")
  string(FIND "${selectionSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Fixed shader-binding lookup regression: hot selection scans '${forbidden}'")
  endif()
endforeach()
