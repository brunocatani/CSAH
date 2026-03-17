#pragma once
#include "PCH.h"
#include "Buffer.h"

struct alignas(16) SharedDataCB {
    float DirLightDirection[4];     // From ShadowState ring +0x140
    float DirLightColor[4];         // From Sky+0x310
    float AmbientColor[6][4];       // From Sky+0x3B8 (6 NiColor, directional ambient)
    float FogParams[4];             // Near, far, power, clamp
    float WeatherData[4];           // Transition alpha, precipitation, wetness, wind
    float Timer;
    uint32_t FrameCount;
    uint32_t IsInterior;
    uint32_t IsVR;
    float Gamma;
    float Padding[3];
};

static_assert(sizeof(SharedDataCB) % 16 == 0, "SharedDataCB must be 16-byte aligned");

class State {
public:
    static State& GetSingleton();

    void Initialize();
    void UpdatePerFrame();
    void BindSharedData();      // Bind SharedDataCB to b3 for both VS and PS

    // Current shader state (set by BeginTechnique hook)
    void* currentShader = nullptr;
    uint32_t currentTechniqueID = 0;

    uint32_t frameCount = 0;

    std::unique_ptr<ConstantBuffer> sharedDataCB;
    SharedDataCB sharedData{};

private:
    State() = default;
    ~State() = default;

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    bool m_initialized = false;
    std::chrono::steady_clock::time_point m_startTime;
};
