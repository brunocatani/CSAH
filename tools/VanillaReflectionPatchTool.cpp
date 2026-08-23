#include "Features/vanilla_fixes/ReflectionCompositePatch.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

int main(int argc, char** argv)
{
    if ((argc != 3 && argc != 4) || !argv[1] || !argv[2]) {
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        return 3;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{},
    };
    const auto bytes = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(raw.data()),
        raw.size(),
    };
    std::vector<std::byte> patched;
    auto patchedReady = false;
    if (argc == 3) {
        patchedReady = community_shaders::vanilla_fixes::
            patchStockReflectionCompositeSurfaceAnchoredCubemap(
                bytes,
                patched);
    } else if (argv[3]) {
        const auto rawMode = std::strtol(argv[3], nullptr, 10);
        if (rawMode >= 1 && rawMode <= 3) {
            patchedReady = community_shaders::vanilla_fixes::
                patchStockReflectionCompositeDirectionalDiagnostic(
                    bytes,
                    static_cast<community_shaders::vanilla_fixes::
                        DirectionalDiagnosticChannel>(rawMode - 1),
                    patched);
        }
    }
    if (!patchedReady) {
        return 4;
    }
    std::ofstream output(argv[2], std::ios::binary);
    if (!output) {
        return 5;
    }
    output.write(
        reinterpret_cast<const char*>(patched.data()),
        static_cast<std::streamsize>(patched.size()));
    return output ? 0 : 6;
}
