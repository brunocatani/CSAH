#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

namespace community_shaders::ui
{
    // Stores the startup settings before the optional provider/UI layer exists.
    void setInitialSettings(
        const linear_lighting::Settings& settings) noexcept;

    // Optional initialization. Missing PrismaUI_F4 or ROCK leaves the renderer
    // and INI owner fully operational without a compatibility fallback.
    void onGameDataReady() noexcept;
    void onGameSessionReady() noexcept;
    void shutdown() noexcept;
}
