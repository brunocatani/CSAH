function(csah_verify_fo4vr_loader source_path)
  if(NOT EXISTS "${source_path}")
    message(FATAL_ERROR "FO4VR loader source is missing: ${source_path}")
  endif()

  file(READ "${source_path}" loader_source)

  foreach(required_text IN ITEMS
      "F4SEPlugin_Query"
      "a_f4se->IsEditor()"
      "REL::Module::IsVR()"
      "const auto requiredRuntime = F4SE::RUNTIME_1_10_138"
      "a_f4se->RuntimeVersion() < requiredRuntime"
      "REL::Module::get().version()"
      "executableVersion != F4SE::RUNTIME_VR_1_2_72")
    string(FIND "${loader_source}" "${required_text}" found_at)
    if(found_at EQUAL -1)
      message(FATAL_ERROR
        "FO4VR loader contract regression: '${required_text}' is absent from ${source_path}")
    endif()
  endforeach()

  if(loader_source MATCHES
      "RuntimeVersion\\(\\)[ \t\r\n]*[=!<>]+[ \t\r\n]*F4SE::RUNTIME_VR")
    message(FATAL_ERROR
      "FO4VR loader contract regression: QueryInterface::RuntimeVersion() was compared with a VR executable-domain constant.")
  endif()

  if(loader_source MATCHES
      "RuntimeVersion\\(\\)[ \t\r\n]*[=!<>]+[ \t\r\n]*F4SE::RUNTIME_LATEST_VR")
    message(FATAL_ERROR
      "FO4VR loader contract regression: QueryInterface::RuntimeVersion() was compared with RUNTIME_LATEST_VR.")
  endif()
endfunction()

if(DEFINED LOADER_SOURCE)
  csah_verify_fo4vr_loader("${LOADER_SOURCE}")
endif()
