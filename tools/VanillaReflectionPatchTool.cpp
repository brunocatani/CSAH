#include "Features/vanilla_fixes/ReflectionCompositePatch.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    if ((argc != 3 && argc != 4) || !argv[1] || !argv[2] ||
        (argc == 4 && !argv[3])) {
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
    const auto mode = argc == 4 ? std::string_view{ argv[3] } :
                                  std::string_view{ "surface-anchor" };
    const auto patchedReady = [&]() noexcept {
        using namespace csah::vanilla_fixes;
        if (mode == "surface-anchor") {
            return patchStockReflectionCompositeSurfaceAnchoredCubemap(
                bytes,
                patched);
        }
        if (mode == "raw-sslr") {
            return patchStockReflectionCompositeRawSslr(bytes, patched);
        }
        if (mode == "raw-stock-cubemap") {
            return patchStockReflectionCompositeRawStockCubemap(
                bytes,
                patched);
        }
        return false;
    }();
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
