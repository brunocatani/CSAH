foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    D3D11_HOOK_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)

foreach(required IN ITEMS
    "ScopedReplacementPixelConstants::ScopedReplacementPixelConstants"
    "ScopedReplacementPixelConstants::~ScopedReplacementPixelConstants"
    "PSGetConstantBuffers(5, 1, &previousFrameBuffer)"
    "PSGetConstantBuffers(8, 1, &previousGeometryBuffer)"
    "PSGetConstantBuffers("
    "previousComplexEnvironmentBuffer"
    "PSSetConstantBuffers(5, 1, &previousFrameBuffer)"
    "PSSetConstantBuffers(8, 1, &previousGeometryBuffer)"
    "ReplacementPixelConstants_ComplexEnvironment"
    "linearLightingEnabled && complexEnvironmentRequested"
    "complexMaterialConsumptionReady()"
    "constantFlags_ & ReplacementPixelConstants_Frame"
    "constantFlags_ & ReplacementPixelConstants_Geometry"
    "binding.family == ReplacementShaderFamily::sky"
    "validMaterial ? geometryBuffer_.Get() : nullptr"
    "scopeReplacementPixelConstants(")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting constant-scope regression: runtime is missing '${required}'")
  endif()
endforeach()

string(FIND "${runtimeSource}"
  "PixelShaderSelection Runtime::selectPixelShader(" selectionStart)
string(FIND "${runtimeSource}"
  "ScopedReplacementPixelConstants Runtime::scopeReplacementPixelConstants("
  scopeStart)
if(selectionStart EQUAL -1 OR scopeStart EQUAL -1 OR
   NOT selectionStart LESS scopeStart)
  message(FATAL_ERROR
    "Linear Lighting constant-scope regression: selection boundary is missing")
endif()
math(EXPR selectionLength "${scopeStart} - ${selectionStart}")
string(SUBSTRING "${runtimeSource}" ${selectionStart} ${selectionLength}
  selectionSource)
string(FIND "${selectionSource}" "PSSetConstantBuffers(" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Linear Lighting constant-scope regression: shader selection mutates engine constant buffers")
endif()

string(FIND "${runtimeSource}"
  "bool Runtime::updateGeometryEmissive(" geometryStart)
string(FIND "${runtimeSource}"
  "void Runtime::setGeometryProviderReady(" providerStart)
if(geometryStart EQUAL -1 OR providerStart EQUAL -1 OR
   NOT geometryStart LESS providerStart)
  message(FATAL_ERROR
    "Linear Lighting constant-scope regression: geometry boundary is missing")
endif()
math(EXPR geometryLength "${providerStart} - ${geometryStart}")
string(SUBSTRING "${runtimeSource}" ${geometryStart} ${geometryLength}
  geometrySource)
string(FIND "${geometrySource}" "PSSetConstantBuffers(" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR
    "Linear Lighting constant-scope regression: geometry update leaks a private constant buffer")
endif()

foreach(required IN ITEMS
    "activeReplacementBinding = selection.binding"
    "scopeActiveReplacementPixelConstants("
    "PipelineBinding_SelectedReplacement")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Linear Lighting constant-scope regression: draw ownership is missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL
  "scopeActiveReplacementPixelConstants\\(context\\)" drawScopes
  "${hookSource}")
list(LENGTH drawScopes drawScopeCount)
if(NOT drawScopeCount EQUAL 4)
  message(FATAL_ERROR
    "Linear Lighting constant-scope regression: all four draw paths must own one restoration scope")
endif()
