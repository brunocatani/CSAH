#include "PCH.h"
#include "Globals.h"
#include "EngineFixes.h"
#include "Hooks.h"
#include "State.h"
#include "ShaderCache.h"
#include "ShaderReplacer.h"
#include "Feature.h"
#include "Menu.h"
#include "Features/LinearLighting.h"

// Global feature instances
static LinearLighting g_linearLighting;

namespace {
    void InitializeLog() {
        auto path = std::filesystem::path("Data/F4SE/Plugins/fo4vr-community-shaders.log");
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    }

    void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* msg) {
        switch (msg->type) {
            case F4SE::MessagingInterface::kPostPostLoad:
            {
                spdlog::info("=== PostPostLoad: Early init (no D3D yet) ===");

                // Detour hooks target code addresses, safe to install before D3D
                Hooks::InstallShaderHooks();

                // Register features (no GPU resources yet)
                Feature::RegisterFeature(&g_linearLighting);

                // Load settings from disk
                Feature::LoadAllSettings("Data/CommunityShaders/Settings/CommunityShaders.json");

                spdlog::info("=== PostPostLoad complete ===");
                break;
            }
            case F4SE::MessagingInterface::kGameDataReady:
            {
                spdlog::info("=== GameDataReady: D3D and game data available ===");

                // NOW initialize globals — D3D device/context should be alive
                Globals::Initialize();

                // Initialize state (creates SharedDataCB — needs device)
                State::GetSingleton().Initialize();

                // Initialize shader cache
                ShaderCache::GetSingleton().Initialize();

                // Initialize features (create GPU resources — needs device)
                Feature::InitializeAll();

                // Install D3D and vtable hooks (needs renderer + vtables)
                Hooks::InstallRenderHooks();
                Hooks::InstallD3DHooks();

                // Apply post-load engine fixes
                EngineFixes::ApplyPostLoadFixes();

                // Start cascade runtime
                EngineFixes::StartCascadeRuntime();

                // Shader replacement will be triggered by the LoadShaders hook
                // when each shader type's FXP is loaded
                spdlog::info("Shader replacement will be triggered by LoadShaders hook");

                // Save settings (captures any defaults)
                Feature::SaveAllSettings("Data/CommunityShaders/Settings/CommunityShaders.json");

                spdlog::info("=== GameDataReady init complete ===");
                break;
            }
        }
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info) {
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "FO4VR Community Shaders";
    a_info->version = 1;
    return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    InitializeLog();
    F4SE::Init(a_f4se);

    spdlog::info("FO4VR Community Shaders v0.1.0 loading");

    // Phase 0: Engine fixes (before game shader system init)
    if (!EngineFixes::ApplyAll()) {
        spdlog::warn("Some engine fixes failed - continuing with partial fixes");
    }

    // Register F4SE message handler
    auto* messaging = F4SE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(OnF4SEMessage);
        spdlog::info("Registered F4SE message listener");
    } else {
        spdlog::error("Failed to get F4SE messaging interface");
        return false;
    }

    spdlog::info("FO4VR Community Shaders v0.1.0 loaded successfully");
    return true;
}
