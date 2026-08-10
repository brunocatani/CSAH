foreach(variable IN ITEMS RENDERER_HOOK_SOURCE RENDERER_VALIDATOR TEST_OUTPUT_DIR)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${RENDERER_HOOK_SOURCE}" validSource)
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

set(identityFixture "${validSource}")
string(REPLACE
  "kNativeScalarPowThunkRva = 0x029917A8"
  "kNativeScalarPowThunkRva = 0x029917A9"
  identityFixture "${identityFixture}")
set(identityFixturePath "${TEST_OUTPUT_DIR}/renderer_identity_invalid.cpp")
file(WRITE "${identityFixturePath}" "${identityFixture}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${identityFixturePath}"
    -P "${RENDERER_VALIDATOR}"
  RESULT_VARIABLE identityResult
  OUTPUT_QUIET
  ERROR_QUIET)
if(identityResult EQUAL 0)
  message(FATAL_ERROR
    "Renderer validator accepted a changed native pow-thunk identity")
endif()

set(techniqueIdentityFixture "${validSource}")
string(REPLACE
  "kTechniqueSetupFunctionRva = 0x02922810"
  "kTechniqueSetupFunctionRva = 0x02922811"
  techniqueIdentityFixture "${techniqueIdentityFixture}")
set(techniqueIdentityFixturePath
  "${TEST_OUTPUT_DIR}/renderer_technique_identity_invalid.cpp")
file(WRITE "${techniqueIdentityFixturePath}" "${techniqueIdentityFixture}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${techniqueIdentityFixturePath}"
    -P "${RENDERER_VALIDATOR}"
  RESULT_VARIABLE techniqueIdentityResult
  OUTPUT_QUIET
  ERROR_QUIET)
if(techniqueIdentityResult EQUAL 0)
  message(FATAL_ERROR
    "Renderer validator accepted a changed DFLight technique identity")
endif()

set(signatureFixture "${validSource}")
string(REPLACE
  "std::byte{ 0xB2 }, std::byte{ 0x2B }, std::byte{ 0x00 }"
  "std::byte{ 0xB3 }, std::byte{ 0x2B }, std::byte{ 0x00 }"
  signatureFixture "${signatureFixture}")
set(signatureFixturePath "${TEST_OUTPUT_DIR}/renderer_signature_invalid.cpp")
file(WRITE "${signatureFixturePath}" "${signatureFixture}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${signatureFixturePath}"
    -P "${RENDERER_VALIDATOR}"
  RESULT_VARIABLE signatureResult
  OUTPUT_QUIET
  ERROR_QUIET)
if(signatureResult EQUAL 0)
  message(FATAL_ERROR
    "Renderer validator accepted a changed native pow-thunk signature")
endif()

set(hotPathFixture "${validSource}")
string(REPLACE
  "const auto active = originalNativeScalarPow &&"
  "VirtualQuery(nullptr, nullptr, 0);\n            const auto active = originalNativeScalarPow &&"
  hotPathFixture "${hotPathFixture}")
set(hotPathFixturePath "${TEST_OUTPUT_DIR}/renderer_hot_path_invalid.cpp")
file(WRITE "${hotPathFixturePath}" "${hotPathFixture}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${hotPathFixturePath}"
    -P "${RENDERER_VALIDATOR}"
  RESULT_VARIABLE hotPathResult
  OUTPUT_QUIET
  ERROR_QUIET)
if(hotPathResult EQUAL 0)
  message(FATAL_ERROR
    "Renderer validator accepted forbidden hot-path VirtualQuery work")
endif()
