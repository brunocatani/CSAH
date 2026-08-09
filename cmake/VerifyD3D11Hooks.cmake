if(NOT DEFINED D3D11_HOOK_SOURCE OR
   NOT EXISTS "${D3D11_HOOK_SOURCE}")
  message(FATAL_ERROR "D3D11_HOOK_SOURCE is missing")
endif()

file(READ "${D3D11_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "#include <MinHook.h>"
    "addressBelongsToModule"
    "GetModuleHandleW(L\"d3d11.dll\")"
    "MH_QueueEnableHook"
    "MH_ApplyQueued"
    "captureMinHookPatchIdentity"
    "detourPatchOwned"
    "shaderInterceptionActive.store(false"
    "shaderInterceptionActive.store(true"
    "resident hooks remain strict pass-through"
    "complete native COM vtables remain untouched"
    "validateD3D11ShaderHooks"
    "original(context, shader, classInstances, classInstanceCount)"
    "no hook repair was attempted")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour regression: missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL "MH_CreateHook\\(" createHookCalls "${source}")
list(LENGTH createHookCalls createHookCallCount)
if(NOT createHookCallCount EQUAL 3)
  message(FATAL_ERROR
    "D3D11 detour regression: expected two shader calls and one bounded draw helper")
endif()

string(FIND "${source}" "Runtime::get().onDeviceCreated" deviceReady)
string(FIND "${source}" "shaderInterceptionActive.store(true" activate)
if(deviceReady EQUAL -1 OR activate EQUAL -1 OR
   NOT deviceReady LESS activate)
  message(FATAL_ERROR
    "D3D11 detour regression: interception activated before GPU initialization")
endif()

string(FIND "${source}" "HookSnapshot d3d11HookSnapshot" snapshotStart)
if(snapshotStart EQUAL -1)
  message(FATAL_ERROR
    "D3D11 detour regression: hook snapshot boundary is missing")
endif()
string(SUBSTRING "${source}" ${snapshotStart} -1 snapshotSource)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "detourPatchOwned(")
  string(FIND "${snapshotSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour hot-path regression: snapshot contains '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "D3D11HookRepairGate"
    "d3d11_hook_repair"
    "immediateContextVtableShadow"
    "kDeviceContextVtableEntryCount"
    "initialContextVtable"
    "immediateContextVtableCell"
    "downstreamPSSetShader"
    "Restored D3D11 immediate-context vtable shadow"
    "Installed isolated D3D11 immediate-context vtable shadow")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour regression: forbidden stale path '${forbidden}'")
  endif()
endforeach()
