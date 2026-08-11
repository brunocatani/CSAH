#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

namespace community_shaders::linear_lighting
{
    [[nodiscard]] Settings loadSettings() noexcept;

    // Updates owned keys individually and preserves unrelated INI content.
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
