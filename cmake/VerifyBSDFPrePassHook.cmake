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
    "std::atomic_uint32_t descriptorConsumerMask"
    "kAllDescriptorConsumers"
    "setDFPrePassLinearLightingEnabled(bool enabled) noexcept"
    "setDFPrePassComplexEnvironmentEnabled(bool enabled) noexcept"
    "setDFPrePassIblEnabled(bool enabled) noexcept"
    "descriptorConsumerMask.load(std::memory_order_acquire)"
    "static_cast<const std::byte*>(pass) + kDescriptorOffset"
    "Never leave hookSetup installed"
    "*cell = expected;"
    "activeDFPrePassDescriptorScope() noexcept")
  string(FIND "${hook_source}" "${required_token}" token_offset)
  if(token_offset EQUAL -1)
    message(FATAL_ERROR
      "BSDFPrePass hook lost required FO4VR identity/scope token: ${required_token}")
  endif()
endforeach()

string(FIND "${hook_source}" "void __fastcall hookSetup(" hook_start)
string(FIND "${hook_source}" "    bool installBSDFPrePassShaderHook()" hook_end)
if(hook_start EQUAL -1 OR hook_end EQUAL -1 OR hook_end LESS_EQUAL hook_start)
  message(FATAL_ERROR "BSDFPrePass hookSetup boundary is unavailable")
endif()
math(EXPR hook_length "${hook_end} - ${hook_start}")
string(SUBSTRING "${hook_source}" ${hook_start} ${hook_length} hook_body)

if(hook_body MATCHES "readable\\(|VirtualQuery\\(")
  message(FATAL_ERROR
    "BSDFPrePass hookSetup must not query virtual memory in the per-draw path")
endif()

string(FIND "${hook_body}"
  "descriptorConsumerMask.load(std::memory_order_acquire)" gate_offset)
string(FIND "${hook_body}"
  "static_cast<const std::byte*>(pass) + kDescriptorOffset" read_offset)
if(gate_offset EQUAL -1 OR read_offset EQUAL -1 OR
   gate_offset GREATER_EQUAL read_offset)
  message(FATAL_ERROR
    "BSDFPrePass descriptor read must remain behind the complete consumer gate")
endif()

if(hook_source MATCHES "REL::|CommonLib|RuntimeVersion")
  message(FATAL_ERROR
    "BSDFPrePass hook must remain independent of unverified CommonLib addresses")
endif()

message(STATUS "Verified FO4VR BSDFPrePass descriptor hook source contract")
