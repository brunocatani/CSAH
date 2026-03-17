#include "PCH.h"

namespace {
    void InitializeLog() {
        auto path = std::filesystem::path("Data/F4SE/Plugins/fo4vr-community-shaders.log");
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    }
}

extern "C" __declspec(dllexport) bool F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    InitializeLog();
    F4SE::Init(a_f4se);
    spdlog::info("FO4VR Community Shaders v0.1.0 loading");
    return true;
}
