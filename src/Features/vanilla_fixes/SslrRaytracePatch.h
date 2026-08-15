#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    [[nodiscard]] bool patchStockSslrRaytracePixel(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
