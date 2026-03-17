#include "State.h"
#include "Globals.h"

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

State& State::GetSingleton()
{
    static State instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Initialize — create shared constant buffer
// ---------------------------------------------------------------------------

void State::Initialize()
{
    if (m_initialized) {
        spdlog::warn("State::Initialize — already initialized, skipping");
        return;
    }

    m_startTime = std::chrono::steady_clock::now();

    sharedDataCB = std::make_unique<ConstantBuffer>(sizeof(SharedDataCB));
    if (!sharedDataCB || !sharedDataCB->IsValid()) {
        spdlog::error("State::Initialize — failed to create SharedDataCB constant buffer");
        return;
    }

    std::memset(&sharedData, 0, sizeof(sharedData));

    // Safe defaults
    sharedData.DirLightDirection[0] = 0.0f;
    sharedData.DirLightDirection[1] = 0.0f;
    sharedData.DirLightDirection[2] = -1.0f;  // straight down
    sharedData.DirLightDirection[3] = 0.0f;

    sharedData.DirLightColor[0] = 1.0f;
    sharedData.DirLightColor[1] = 1.0f;
    sharedData.DirLightColor[2] = 1.0f;
    sharedData.DirLightColor[3] = 1.0f;

    // Neutral ambient
    for (int i = 0; i < 6; ++i) {
        sharedData.AmbientColor[i][0] = 0.1f;
        sharedData.AmbientColor[i][1] = 0.1f;
        sharedData.AmbientColor[i][2] = 0.1f;
        sharedData.AmbientColor[i][3] = 1.0f;
    }

    sharedData.FogParams[0] = 0.0f;     // near
    sharedData.FogParams[1] = 10000.0f;  // far
    sharedData.FogParams[2] = 1.0f;      // power
    sharedData.FogParams[3] = 0.0f;      // density

    sharedData.IsVR = Globals::IsVR() ? 1u : 0u;
    sharedData.Gamma = 1.0f;

    m_initialized = true;
    spdlog::info("State::Initialize — SharedDataCB created ({} bytes), IsVR={}", sizeof(SharedDataCB), sharedData.IsVR);
}

// ---------------------------------------------------------------------------
// UpdatePerFrame — fill SharedDataCB from game state
// ---------------------------------------------------------------------------

void State::UpdatePerFrame()
{
    if (!m_initialized) {
        return;
    }

    // ---- Timer ----
    auto now = std::chrono::steady_clock::now();
    sharedData.Timer = std::chrono::duration<float>(now - m_startTime).count();

    // ---- Frame count ----
    frameCount++;
    sharedData.FrameCount = frameCount;

    // ---- VR flag (constant but kept updated for safety) ----
    sharedData.IsVR = Globals::IsVR() ? 1u : 0u;

    // ---- Sun direction from ShadowState ring buffer ----
    // ShadowState ring base: Globals::GetShadowStateRing() (base+0x68780D0)
    // Read index at base+0x68780C4
    // Buffer stride: 0x1A0
    // Direction vector at +0x140 within each entry
    {
        uintptr_t base = Globals::GetBase();
        if (base) {
            auto* readIndex = reinterpret_cast<uint32_t*>(base + 0x68780C4);
            uintptr_t ringBase = Globals::GetShadowStateRing();

            if (readIndex && ringBase) {
                uint32_t idx = *readIndex;
                // Ring buffer has a small number of entries; clamp to reasonable range
                if (idx < 16) {
                    uintptr_t entry = ringBase + static_cast<uintptr_t>(idx) * 0x1A0;
                    auto* dirVec = reinterpret_cast<float*>(entry + 0x140);

                    // Validate — direction vector should be roughly unit length
                    float lenSq = dirVec[0] * dirVec[0] + dirVec[1] * dirVec[1] + dirVec[2] * dirVec[2];
                    if (lenSq > 0.01f && lenSq < 10.0f) {
                        sharedData.DirLightDirection[0] = dirVec[0];
                        sharedData.DirLightDirection[1] = dirVec[1];
                        sharedData.DirLightDirection[2] = dirVec[2];
                        sharedData.DirLightDirection[3] = 0.0f;
                    } else {
                        spdlog::trace("State::UpdatePerFrame — sun direction lenSq={:.4f} out of range, keeping previous", lenSq);
                    }
                }
            }
        }
    }

    // ---- Interior flag ----
    // TES::GetSingleton()->IsInterior() — check via Sky pointer validity heuristic
    // If Sky is null or its currentWeather is null, likely in a loading screen
    {
        uintptr_t sky = Globals::GetSky();
        if (sky) {
            // Sky+0x68 is typically the cell owner; for interiors, check player cell
            // For now, use a safe default — will be refined when cell hooks are added
            // TODO: Read interior flag from player cell (TESObjectCELL::IsInterior)
            sharedData.IsInterior = 0u;
        }
    }

    // ---- Sun color ----
    // TODO: Read from Sky+0x310 (directional light color) once Sky structure is mapped
    // For now, keep the default white

    // ---- Ambient colors ----
    // TODO: Read from Sky+0x3B8 (6 directional ambient NiColor values)

    // ---- Fog parameters ----
    // TODO: Read from FogGlobals region (Globals::GetFogGlobals()) once layout is mapped
    // FogGlobals at base+0x65A2AC4 contains near/far/power values

    // ---- Weather data ----
    // TODO: Read weather transition, precipitation, wetness from Sky weather state

    // ---- Gamma ----
    // TODO: Read from ImageSpaceManager or INI setting
    sharedData.Gamma = 1.0f;

    spdlog::trace("State::UpdatePerFrame — frame={}, timer={:.2f}s, sunDir=({:.3f},{:.3f},{:.3f})",
        sharedData.FrameCount, sharedData.Timer,
        sharedData.DirLightDirection[0], sharedData.DirLightDirection[1], sharedData.DirLightDirection[2]);
}

// ---------------------------------------------------------------------------
// BindSharedData — upload CB and bind to b3 for VS + PS
// ---------------------------------------------------------------------------

void State::BindSharedData()
{
    if (!sharedDataCB || !sharedDataCB->IsValid()) {
        return;
    }

    sharedDataCB->Update(&sharedData, sizeof(SharedDataCB));
    sharedDataCB->VSBind(3);  // b3
    sharedDataCB->PSBind(3);  // b3
}
