#pragma once

#include "Features/complex_materials/ComplexParallaxSettings.h"

namespace community_shaders::complex_materials
{
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
