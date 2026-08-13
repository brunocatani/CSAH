if(NOT DEFINED DFPREPASS_HOOK_SOURCE OR
   NOT EXISTS "${DFPREPASS_HOOK_SOURCE}")
  message(FATAL_ERROR "BSDFPrePass hook source is unavailable")
endif()

file(READ "${DFPREPASS_HOOK_SOURCE}" hook_source)
foreach(required_token IN ITEMS
    "kVtableRva = 0x030B8C68"
    "kSetupSlot = 3"
    "kSetupFunctionRva = 0x02878F80"
    "kDescriptorOffset = 0x48"
    "std::array<std::byte, 49> kSetupSignature"
    "std::memcmp("
    "thread_local DFPrePassDescriptorScope activeScope"
    "const DescriptorScope scope(descriptor)"
    "if (pass && readable("
    "Never leave hookSetup installed"
    "*cell = expected;"
    "activeDFPrePassDescriptorScope() noexcept")
  string(FIND "${hook_source}" "${required_token}" token_offset)
  if(token_offset EQUAL -1)
    message(FATAL_ERROR
      "BSDFPrePass hook lost required FO4VR identity/scope token: ${required_token}")
  endif()
endforeach()

if(hook_source MATCHES "REL::|CommonLib|RuntimeVersion")
  message(FATAL_ERROR
    "BSDFPrePass hook must remain independent of unverified CommonLib addresses")
endif()

message(STATUS "Verified FO4VR BSDFPrePass descriptor hook source contract")
