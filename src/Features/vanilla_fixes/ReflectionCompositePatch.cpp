#include "Features/vanilla_fixes/ReflectionCompositePatch.h"

#include "Features/linear_lighting/DxbcChecksum.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::size_t kDeclaredSizeOffset = 24;
        constexpr std::size_t kChunkCountOffset = 28;
        constexpr std::size_t kChunkOffsetsOffset = 32;
        constexpr std::uint32_t kShexTag = 0x58454853u;
        constexpr std::uint32_t kShdrTag = 0x52444853u;

        // Exact regular and conditional camera-buffer declarations. The
        // surface-anchored path reads ordinary inverse projections at rows
        // 32..39 and live eye origins at rows 59/60. The regular permutation
        // therefore needs its stock 51-row declaration extended to 61 rows;
        // the conditional declaration already exposes 77 rows.
        constexpr std::array<std::uint32_t, 4>
            kStockRegularCameraDeclaration{
                0x04000859u,
                0x00208E46u,
                0x0000000Cu,
                0x00000033u,
            };
        constexpr std::array<std::uint32_t, 4>
            kSurfaceAnchoredRegularCameraDeclaration{
                0x04000859u,
                0x00208E46u,
                0x0000000Cu,
                0x0000003Du,
            };
        constexpr std::array<std::uint32_t, 4>
            kConditionalCameraDeclaration{
                0x04000859u,
                0x00208E46u,
                0x0000000Cu,
                0x0000004Du,
            };

        // Exact stock instruction at SHEX word 693:
        // sample_l r4.xyz, r5.xyzw, t8.xyzw, s8, r0.z
        constexpr std::array<std::uint32_t, 13> kStockCubemapSample{
            0x8D000048u,
            0x80000282u,
            0x00155543u,
            0x00100072u,
            0x00000004u,
            0x00100E46u,
            0x00000005u,
            0x00107E46u,
            0x00000008u,
            0x00106000u,
            0x00000008u,
            0x0010002Au,
            0x00000000u,
        };

        // mov r4.xyz, l(0,0,0,0)
        constexpr std::array<std::uint32_t, 8> kBlackCubemapSample{
            0x08000036u,
            0x00100072u,
            0x00000004u,
            0x00004002u,
            0x00000000u,
            0x00000000u,
            0x00000000u,
            0x00000000u,
        };

        // Stock eye-matrix selector:
        // ishl r1.w, v1.x, l(2)
        constexpr std::array<std::uint32_t, 7> kStockEyeMatrixOffset{
            0x07000029u,
            0x00100082u,
            0x00000001u,
            0x0010100Au,
            0x00000001u,
            0x00004001u,
            0x00000002u,
        };

        // Exact depth<=0.01 load families. Live CB12 capture found rows 40..47
        // non-finite, so both layouts are redirected to their valid ordinary
        // inverse-projection groups at rows 32..39.
        constexpr std::array<std::uint32_t, 32>
            kStockRegularCompressedMatrices{
                0x08000036u, 0x001000F2u, 0x00000005u, 0x06208E46u,
                0x0000000Cu, 0x00000028u, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000006u, 0x06208E46u,
                0x0000000Cu, 0x00000029u, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000007u, 0x06208E46u,
                0x0000000Cu, 0x0000002Au, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000008u, 0x06208E46u,
                0x0000000Cu, 0x0000002Bu, 0x0010003Au, 0x00000001u,
            };

        constexpr std::array<std::uint32_t, 32>
            kStockConditionalCompressedMatrices{
                0x08000036u, 0x001000F2u, 0x00000004u, 0x06208E46u,
                0x0000000Cu, 0x00000028u, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000005u, 0x06208E46u,
                0x0000000Cu, 0x00000029u, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000006u, 0x06208E46u,
                0x0000000Cu, 0x0000002Au, 0x0010003Au, 0x00000001u,
                0x08000036u, 0x001000F2u, 0x00000007u, 0x06208E46u,
                0x0000000Cu, 0x0000002Bu, 0x0010003Au, 0x00000001u,
            };

        // Stock local-X eye selector:
        // mov r0.z, v1.x
        constexpr std::array<std::uint32_t, 5> kStockEyeLocalIndex{
            0x05000036u,
            0x00100042u,
            0x00000000u,
            0x0010100Au,
            0x00000001u,
        };

        // Exact stock position reconstruction immediately before the regular
        // permutation normalizes the surface-to-eye vector.
        constexpr std::array<std::uint32_t, 7>
            kStockRegularPositionReconstruction{
                0x0700000Eu,
                0x00100072u,
                0x00000004u,
                0x00100246u,
                0x00000005u,
                0x00100AA6u,
                0x00000000u,
            };

        // Reconstruct the eye-relative surface from packed-eye-local X and a
        // freshly reloaded ordinary inverse projection. The stock dp4 sequence
        // immediately before this insertion overwrites r5.x with its first
        // result, so reusing the previously selected r5..r8 matrix would be
        // invalid. Reloading all four rows also bypasses the non-finite
        // compressed-near matrices at CB12[40..47].
        constexpr std::array<std::uint32_t, 95> kRegularSurfaceRaySetup{
            // mul r4.x, r0.x, l(2)
            0x07000038u, 0x00100012u, 0x00000004u, 0x0010000Au,
            0x00000000u, 0x00004001u, 0x40000000u,
            // frc r4.x, r4.x
            0x0500001Au, 0x00100012u, 0x00000004u, 0x0010000Au,
            0x00000004u,
            // mad r4.x, r4.x, l(2), l(-1)
            0x09000032u, 0x00100012u, 0x00000004u, 0x0010000Au,
            0x00000004u, 0x00004001u, 0x40000000u, 0x00004001u,
            0xBF800000u,
            // ge/and r0.z = packed eye matrix offset 0 or 4
            0x0700001Du, 0x00100042u, 0x00000000u, 0x0010000Au,
            0x00000000u, 0x00004001u, 0x3F000000u,
            0x07000001u, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x00004001u, 0x00000004u,
            // mov r5..r8, cb12[r0.z + 32..35]
            0x08000036u, 0x001000F2u, 0x00000005u, 0x06208E46u,
            0x0000000Cu, 0x00000020u, 0x0010002Au, 0x00000000u,
            0x08000036u, 0x001000F2u, 0x00000006u, 0x06208E46u,
            0x0000000Cu, 0x00000021u, 0x0010002Au, 0x00000000u,
            0x08000036u, 0x001000F2u, 0x00000007u, 0x06208E46u,
            0x0000000Cu, 0x00000022u, 0x0010002Au, 0x00000000u,
            0x08000036u, 0x001000F2u, 0x00000008u, 0x06208E46u,
            0x0000000Cu, 0x00000023u, 0x0010002Au, 0x00000000u,
            // dp4 r5.xyz/r0.z, reloaded inverse projection, r4.xyzw
            0x07000011u, 0x00100012u, 0x00000005u, 0x00100D86u,
            0x00000005u, 0x00100E46u, 0x00000004u,
            0x07000011u, 0x00100022u, 0x00000005u, 0x00100D86u,
            0x00000006u, 0x00100E46u, 0x00000004u,
            0x07000011u, 0x00100042u, 0x00000005u, 0x00100D86u,
            0x00000007u, 0x00100E46u, 0x00000004u,
            0x07000011u, 0x00100042u, 0x00000000u, 0x00100D86u,
            0x00000008u, 0x00100E46u, 0x00000004u,
        };

        // Convert the reconstructed eye-relative surface into a ray from the
        // midpoint between the live eye origins. CB12[59] is paired with the
        // first ordinary inverse projection and CB12[60] with the second; the
        // captured world-space difference becomes pure view-space X through
        // CB12[0..2]. The eye bit is recomputed because the stock shader has
        // already reused its original matrix-selector register.
        constexpr std::array<std::uint32_t, 63>
            kRegularSurfaceAnchorCorrection{
                // ge/and r0.z = packed eye index 0 or 1
                0x0700001Du, 0x00100042u, 0x00000000u, 0x0010000Au,
                0x00000000u, 0x00004001u, 0x3F000000u,
                0x07000001u, 0x00100042u, 0x00000000u, 0x0010002Au,
                0x00000000u, 0x00004001u, 0x00000001u,
                // r5 = (origin59 - origin60) * { +0.5, -0.5 }[eye]
                0x0A000000u, 0x00100072u, 0x00000005u, 0x00208246u,
                0x0000000Cu, 0x0000003Bu, 0x80208246u, 0x00000041u,
                0x0000000Cu, 0x0000003Cu,
                0x08000038u, 0x00100072u, 0x00000005u, 0x00100246u,
                0x00000005u, 0x00909006u, 0x0010002Au, 0x00000000u,
                // world-space eye offset to view-space r6
                0x08000010u, 0x00100012u, 0x00000006u, 0x00208246u,
                0x0000000Cu, 0x00000000u, 0x00100246u, 0x00000005u,
                0x08000010u, 0x00100022u, 0x00000006u, 0x00208246u,
                0x0000000Cu, 0x00000001u, 0x00100246u, 0x00000005u,
                0x08000010u, 0x00100042u, 0x00000006u, 0x00208246u,
                0x0000000Cu, 0x00000002u, 0x00100246u, 0x00000005u,
                // add r4.xyz, r4.xyzx, r6.xyzx
                0x07000000u, 0x00100072u, 0x00000004u, 0x00100246u,
                0x00000004u, 0x00100246u, 0x00000006u,
            };

        constexpr std::array<std::uint32_t, 7>
            kStockConditionalPositionReconstruction{
                0x0700000Eu,
                0x00100072u,
                0x00000003u,
                0x00100246u,
                0x00000004u,
                0x00100FF6u,
                0x00000000u,
            };

        // Exact final dot products between the reconstructed incident vector
        // and decoded G-buffer normal.
        constexpr std::array<std::uint32_t, 7> kStockRegularReflectionDot{
            0x07000010u,
            0x00100042u,
            0x00000000u,
            0x00100346u,
            0x00000004u,
            0x00100246u,
            0x0000000Au,
        };

        constexpr std::array<std::uint32_t, 7>
            kStockConditionalReflectionDot{
                0x07000010u,
                0x00100082u,
                0x00000000u,
                0x00100246u,
                0x00000006u,
                0x00100246u,
                0x00000007u,
            };

        constexpr std::array<std::uint32_t, 95>
            kConditionalSurfaceRaySetup{
                // mul r3.x, r0.x, l(2)
                0x07000038u, 0x00100012u, 0x00000003u, 0x0010000Au,
                0x00000000u, 0x00004001u, 0x40000000u,
                // frc r3.x, r3.x
                0x0500001Au, 0x00100012u, 0x00000003u, 0x0010000Au,
                0x00000003u,
                // mad r3.x, r3.x, l(2), l(-1)
                0x09000032u, 0x00100012u, 0x00000003u, 0x0010000Au,
                0x00000003u, 0x00004001u, 0x40000000u, 0x00004001u,
                0xBF800000u,
                // ge/and r0.w = packed eye matrix offset 0 or 4
                0x0700001Du, 0x00100082u, 0x00000000u, 0x0010000Au,
                0x00000000u, 0x00004001u, 0x3F000000u,
                0x07000001u, 0x00100082u, 0x00000000u, 0x0010003Au,
                0x00000000u, 0x00004001u, 0x00000004u,
                // mov r4..r7, cb12[r0.w + 32..35]
                0x08000036u, 0x001000F2u, 0x00000004u, 0x06208E46u,
                0x0000000Cu, 0x00000020u, 0x0010003Au, 0x00000000u,
                0x08000036u, 0x001000F2u, 0x00000005u, 0x06208E46u,
                0x0000000Cu, 0x00000021u, 0x0010003Au, 0x00000000u,
                0x08000036u, 0x001000F2u, 0x00000006u, 0x06208E46u,
                0x0000000Cu, 0x00000022u, 0x0010003Au, 0x00000000u,
                0x08000036u, 0x001000F2u, 0x00000007u, 0x06208E46u,
                0x0000000Cu, 0x00000023u, 0x0010003Au, 0x00000000u,
                // dp4 r4.xyz/r0.w, reloaded inverse projection, r3.xyzw
                0x07000011u, 0x00100012u, 0x00000004u, 0x00100D86u,
                0x00000004u, 0x00100E46u, 0x00000003u,
                0x07000011u, 0x00100022u, 0x00000004u, 0x00100D86u,
                0x00000005u, 0x00100E46u, 0x00000003u,
                0x07000011u, 0x00100042u, 0x00000004u, 0x00100D86u,
                0x00000006u, 0x00100E46u, 0x00000003u,
                0x07000011u, 0x00100082u, 0x00000000u, 0x00100D86u,
                0x00000007u, 0x00100E46u, 0x00000003u,
            };

        constexpr std::array<std::uint32_t, 63>
            kConditionalSurfaceAnchorCorrection{
                // ge/and r0.w = packed eye index 0 or 1
                0x0700001Du, 0x00100082u, 0x00000000u, 0x0010000Au,
                0x00000000u, 0x00004001u, 0x3F000000u,
                0x07000001u, 0x00100082u, 0x00000000u, 0x0010003Au,
                0x00000000u, 0x00004001u, 0x00000001u,
                // r4 = (origin59 - origin60) * { +0.5, -0.5 }[eye]
                0x0A000000u, 0x00100072u, 0x00000004u, 0x00208246u,
                0x0000000Cu, 0x0000003Bu, 0x80208246u, 0x00000041u,
                0x0000000Cu, 0x0000003Cu,
                0x08000038u, 0x00100072u, 0x00000004u, 0x00100246u,
                0x00000004u, 0x00909006u, 0x0010003Au, 0x00000000u,
                // world-space eye offset to view-space r5
                0x08000010u, 0x00100012u, 0x00000005u, 0x00208246u,
                0x0000000Cu, 0x00000000u, 0x00100246u, 0x00000004u,
                0x08000010u, 0x00100022u, 0x00000005u, 0x00208246u,
                0x0000000Cu, 0x00000001u, 0x00100246u, 0x00000004u,
                0x08000010u, 0x00100042u, 0x00000005u, 0x00208246u,
                0x0000000Cu, 0x00000002u, 0x00100246u, 0x00000004u,
                // add r3.xyz, r3.xyzx, r5.xyzx
                0x07000000u, 0x00100072u, 0x00000003u, 0x00100246u,
                0x00000003u, 0x00100246u, 0x00000005u,
            };

        // Stock instructions immediately after the t14 sample:
        // alpha = min(t14.a * cb0[2].z, 1)
        // r4 = lerp(r4, t14.rgb * cb0[1].x, alpha)
        constexpr std::array<std::uint32_t, 35> kStockSslrAlphaBlend{
            0x08000038u, 0x00100042u, 0x00000000u, 0x0010003Au,
            0x00000005u, 0x0020802Au, 0x00000000u, 0x00000002u,
            0x07000033u, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x00004001u, 0x3F800000u,
            0x0B000032u, 0x00100072u, 0x00000005u, 0x00100246u,
            0x00000005u, 0x00208006u, 0x00000000u, 0x00000001u,
            0x80100246u, 0x00000041u, 0x00000004u,
            0x09000032u, 0x00100072u, 0x00000004u, 0x00100AA6u,
            0x00000000u, 0x00100246u, 0x00000005u, 0x00100246u,
            0x00000004u,
        };

        // mul r4.xyz, r5.xyzx, cb0[1].xxxx
        constexpr std::array<std::uint32_t, 8> kRawSslrColor{
            0x08000038u,
            0x00100072u,
            0x00000004u,
            0x00100246u,
            0x00000005u,
            0x00208006u,
            0x00000000u,
            0x00000001u,
        };

        // Exact stock final RGB output:
        // mul o0.xyz, r0.xxxx, r1.xyzx
        constexpr std::array<std::uint32_t, 7> kStockFinalCompositeOutput{
            0x07000038u,
            0x00102072u,
            0x00000000u,
            0x00100006u,
            0x00000000u,
            0x00100246u,
            0x00000001u,
        };

        // mov o0.xyz, r4.xyzx
        constexpr std::array<std::uint32_t, 5> kRawSslrFinalOutput{
            0x05000036u,
            0x00102072u,
            0x00000000u,
            0x00100246u,
            0x00000004u,
        };

        // mov o0.xyz, r4.xyzx. The stock cube-array sample and saturation
        // processing leave the uncorrected cubemap in r4.
        constexpr std::array<std::uint32_t, 5> kRawCubemapFinalOutput{
            0x05000036u,
            0x00102072u,
            0x00000000u,
            0x00100246u,
            0x00000004u,
        };

        // Exact stock cube-array sample in the conditional siblings:
        // sample_l r5.xzw, r7.xyzw, t8.xwyz, s8, r0.w
        constexpr std::array<std::uint32_t, 13>
            kConditionalCubemapSample{
                0x8D000048u,
                0x80000282u,
                0x00155543u,
                0x001000D2u,
                0x00000005u,
                0x00100E46u,
                0x00000007u,
                0x001079C6u,
                0x00000008u,
                0x00106000u,
                0x00000008u,
                0x0010003Au,
                0x00000000u,
            };

        // Exact t14 sample in the conditional 11316-byte DFComposite sibling:
        // sample r6.xyzw, r0.xyxx, t14.xyzw, s14
        constexpr std::array<std::uint32_t, 11> kConditionalSslrSample{
            0x8B000045u,
            0x800000C2u,
            0x00155543u,
            0x001000F2u,
            0x00000006u,
            0x00100046u,
            0x00000000u,
            0x00107E46u,
            0x0000000Eu,
            0x00106000u,
            0x0000000Eu,
        };

        // Exact final RGB instruction in that sibling:
        // mad o0.xyz, r0.xxxx, r0.yzwy, r2.xyzx
        constexpr std::array<std::uint32_t, 9>
            kStockConditionalFinalOutput{
                0x09000032u,
                0x00102072u,
                0x00000000u,
                0x00100006u,
                0x00000000u,
                0x00100796u,
                0x00000000u,
                0x00100246u,
                0x00000002u,
            };

        // mul o0.xyz, r6.xyzx, cb0[1].xxxx
        constexpr std::array<std::uint32_t, 8>
            kConditionalRawSslrFinalOutput{
                0x08000038u,
                0x00102072u,
                0x00000000u,
                0x00100246u,
                0x00000006u,
                0x00208006u,
                0x00000000u,
                0x00000001u,
            };

        // mov r6.xyzw, r5.xzwx. Stage the stock cubemap at the t14 sample
        // location because the remaining conditional code repurposes r5.
        constexpr std::array<std::uint32_t, 5>
            kConditionalCubemapStage{
                0x05000036u,
                0x001000F2u,
                0x00000006u,
                0x00100386u,
                0x00000005u,
            };

        // mov o0.xyz, r6.xyzx
        constexpr std::array<std::uint32_t, 5>
            kConditionalRawCubemapFinalOutput{
                0x05000036u,
                0x00102072u,
                0x00000000u,
                0x00100246u,
                0x00000006u,
            };

        enum class CompositePatchMode : std::uint8_t
        {
            surfaceAnchoredCubemap,
            rawSslrIsolation,
            rawStockCubemapIsolation,
        };

        struct Chunk
        {
            std::uint32_t offset{};
            std::uint32_t size{};
            std::uint32_t tag{};
        };

        [[nodiscard]] bool readU32(
            std::span<const std::byte> bytes,
            std::size_t offset,
            std::uint32_t& value) noexcept
        {
            if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return true;
        }

        [[nodiscard]] bool writeU32(
            std::span<std::byte> bytes,
            std::size_t offset,
            std::uint32_t value) noexcept
        {
            if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
            return true;
        }

        template <std::size_t Size>
        [[nodiscard]] std::optional<std::size_t> findUniqueSequence(
            std::span<const std::uint32_t> words,
            const std::array<std::uint32_t, Size>& sequence) noexcept
        {
            std::optional<std::size_t> match;
            if (sequence.size() > words.size()) {
                return match;
            }
            for (std::size_t index = 0;
                 index <= words.size() - sequence.size();
                 ++index) {
                if (!std::equal(
                        sequence.begin(),
                        sequence.end(),
                        words.begin() + index)) {
                    continue;
                }
                if (match) {
                    return std::nullopt;
                }
                match = index;
            }
            return match;
        }

        [[nodiscard]] bool validateChunks(
            std::span<const std::byte> bytes,
            std::vector<Chunk>& chunks,
            std::size_t& shaderChunkIndex) noexcept
        {
            constexpr std::array<std::byte, 4> magic{
                std::byte{ 0x44 },
                std::byte{ 0x58 },
                std::byte{ 0x42 },
                std::byte{ 0x43 },
            };
            std::uint32_t declaredSize{};
            std::uint32_t chunkCount{};
            if (bytes.size() < kChunkOffsetsOffset ||
                !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
                !readU32(bytes, kDeclaredSizeOffset, declaredSize) ||
                declaredSize != bytes.size() ||
                !readU32(bytes, kChunkCountOffset, chunkCount) ||
                chunkCount == 0 ||
                chunkCount > (bytes.size() - kChunkOffsetsOffset) /
                    sizeof(std::uint32_t)) {
                return false;
            }

            const auto headerSize = kChunkOffsetsOffset +
                static_cast<std::size_t>(chunkCount) * sizeof(std::uint32_t);
            chunks.clear();
            chunks.reserve(chunkCount);
            std::optional<std::size_t> shaderIndex;
            for (std::uint32_t index = 0; index < chunkCount; ++index) {
                Chunk chunk{};
                if (!readU32(
                        bytes,
                        kChunkOffsetsOffset +
                            static_cast<std::size_t>(index) *
                                sizeof(std::uint32_t),
                        chunk.offset) ||
                    chunk.offset < headerSize ||
                    chunk.offset > bytes.size() ||
                    8 > bytes.size() - chunk.offset ||
                    !readU32(bytes, chunk.offset, chunk.tag) ||
                    !readU32(bytes, chunk.offset + 4, chunk.size) ||
                    chunk.size > bytes.size() - chunk.offset - 8) {
                    return false;
                }
                if (chunk.tag == kShexTag || chunk.tag == kShdrTag) {
                    if (shaderIndex) {
                        return false;
                    }
                    shaderIndex = chunks.size();
                }
                chunks.push_back(chunk);
            }
            if (!shaderIndex) {
                return false;
            }

            auto sortedChunks = chunks;
            std::ranges::sort(sortedChunks, {}, &Chunk::offset);
            for (std::size_t index = 1; index < sortedChunks.size(); ++index) {
                const auto previousEnd =
                    static_cast<std::size_t>(sortedChunks[index - 1].offset) +
                    8 + sortedChunks[index - 1].size;
                if (sortedChunks[index].offset < previousEnd) {
                    return false;
                }
            }
            shaderChunkIndex = *shaderIndex;
            return true;
        }
    }

    namespace
    {
        bool patchStockReflectionComposite(
            std::span<const std::byte> stockBytecode,
            std::vector<std::byte>& patchedBytecode,
            const CompositePatchMode mode) noexcept
        {
        patchedBytecode.clear();
        try {
            std::vector<Chunk> chunks;
            std::size_t shaderChunkIndex{};
            if (!validateChunks(
                    stockBytecode,
                    chunks,
                    shaderChunkIndex)) {
                return false;
            }
            const auto& shaderChunk = chunks[shaderChunkIndex];
            if (shaderChunk.size < 2 * sizeof(std::uint32_t) ||
                shaderChunk.size % sizeof(std::uint32_t) != 0) {
                return false;
            }

            const auto shaderPayloadOffset =
                static_cast<std::size_t>(shaderChunk.offset) + 8;
            std::vector<std::uint32_t> words(
                shaderChunk.size / sizeof(std::uint32_t));
            std::memcpy(
                words.data(),
                stockBytecode.data() + shaderPayloadOffset,
                shaderChunk.size);
            if (words[1] != words.size()) {
                return false;
            }

            const auto samplePosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockCubemapSample);
            const auto regularCameraDeclarationPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockRegularCameraDeclaration);
            const auto conditionalCameraDeclarationPosition =
                findUniqueSequence(
                    std::span<const std::uint32_t>{ words },
                    kConditionalCameraDeclaration);
            const auto eyeMatrixPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockEyeMatrixOffset);
            const auto regularCompressedPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockRegularCompressedMatrices);
            const auto conditionalCompressedPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockConditionalCompressedMatrices);
            const auto eyeLocalPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockEyeLocalIndex);
            const auto blendPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockSslrAlphaBlend);
            const auto outputPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockFinalCompositeOutput);
            const auto regularReconstructionPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockRegularPositionReconstruction);
            const auto conditionalSamplePosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kConditionalSslrSample);
            const auto conditionalCubemapPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kConditionalCubemapSample);
            const auto conditionalOutputPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockConditionalFinalOutput);
            const auto conditionalReconstructionPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockConditionalPositionReconstruction);
            const auto regularReflectionDotPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockRegularReflectionDot);
            const auto conditionalReflectionDotPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockConditionalReflectionDot);
            const auto regularLayout = samplePosition &&
                regularCameraDeclarationPosition && eyeMatrixPosition &&
                regularCompressedPosition &&
                eyeLocalPosition && regularReconstructionPosition &&
                regularReflectionDotPosition && blendPosition &&
                outputPosition &&
                *eyeMatrixPosition + kStockEyeMatrixOffset.size() <=
                    *regularCompressedPosition &&
                *regularCompressedPosition +
                        kStockRegularCompressedMatrices.size() <=
                    *eyeLocalPosition &&
                *eyeLocalPosition + kStockEyeLocalIndex.size() <=
                    *regularReconstructionPosition &&
                *regularReconstructionPosition +
                        kStockRegularPositionReconstruction.size() <=
                    *regularReflectionDotPosition &&
                *regularReflectionDotPosition +
                        kStockRegularReflectionDot.size() <=
                    *samplePosition &&
                *samplePosition + kStockCubemapSample.size() <=
                    *blendPosition &&
                *blendPosition + kStockSslrAlphaBlend.size() <=
                    *outputPosition;
            const auto conditionalLayout = conditionalCubemapPosition &&
                conditionalCameraDeclarationPosition && eyeMatrixPosition &&
                conditionalCompressedPosition &&
                eyeLocalPosition && conditionalReconstructionPosition &&
                conditionalReflectionDotPosition &&
                conditionalSamplePosition && conditionalOutputPosition &&
                *eyeMatrixPosition + kStockEyeMatrixOffset.size() <=
                    *conditionalCompressedPosition &&
                *conditionalCompressedPosition +
                        kStockConditionalCompressedMatrices.size() <=
                    *eyeLocalPosition &&
                *eyeLocalPosition + kStockEyeLocalIndex.size() <=
                    *conditionalReconstructionPosition &&
                *conditionalReconstructionPosition +
                        kStockConditionalPositionReconstruction.size() <=
                    *conditionalReflectionDotPosition &&
                *conditionalReflectionDotPosition +
                        kStockConditionalReflectionDot.size() <=
                    *conditionalCubemapPosition &&
                *conditionalCubemapPosition +
                        kConditionalCubemapSample.size() <=
                    *conditionalSamplePosition &&
                *conditionalSamplePosition + kConditionalSslrSample.size() <=
                    *conditionalOutputPosition;
            if (regularLayout == conditionalLayout) {
                return false;
            }
            if (regularLayout) {
                if (mode == CompositePatchMode::rawSslrIsolation) {
                    words.erase(
                        words.begin() + *outputPosition,
                        words.begin() + *outputPosition +
                            kStockFinalCompositeOutput.size());
                    words.insert(
                        words.begin() + *outputPosition,
                        kRawSslrFinalOutput.begin(),
                        kRawSslrFinalOutput.end());
                    words.erase(
                        words.begin() + *blendPosition,
                        words.begin() + *blendPosition +
                            kStockSslrAlphaBlend.size());
                    words.insert(
                        words.begin() + *blendPosition,
                        kRawSslrColor.begin(),
                        kRawSslrColor.end());
                    words.erase(
                        words.begin() + *samplePosition,
                        words.begin() + *samplePosition +
                            kStockCubemapSample.size());
                    words.insert(
                        words.begin() + *samplePosition,
                        kBlackCubemapSample.begin(),
                        kBlackCubemapSample.end());
                } else {
                    if (mode == CompositePatchMode::
                            rawStockCubemapIsolation) {
                        words.erase(
                            words.begin() + *outputPosition,
                            words.begin() + *outputPosition +
                                kStockFinalCompositeOutput.size());
                        words.insert(
                            words.begin() + *outputPosition,
                            kRawCubemapFinalOutput.begin(),
                            kRawCubemapFinalOutput.end());
                        words.erase(
                            words.begin() + *blendPosition,
                            words.begin() + *blendPosition +
                                kStockSslrAlphaBlend.size());
                    }
                    if (mode == CompositePatchMode::surfaceAnchoredCubemap) {
                        words.insert(
                            words.begin() + *regularReconstructionPosition +
                                kStockRegularPositionReconstruction.size(),
                            kRegularSurfaceAnchorCorrection.begin(),
                            kRegularSurfaceAnchorCorrection.end());
                        words.insert(
                            words.begin() + *regularReconstructionPosition,
                            kRegularSurfaceRaySetup.begin(),
                            kRegularSurfaceRaySetup.end());
                        std::copy(
                            kSurfaceAnchoredRegularCameraDeclaration.begin(),
                            kSurfaceAnchoredRegularCameraDeclaration.end(),
                            words.begin() + *regularCameraDeclarationPosition);
                        words[*regularCompressedPosition + 5] = 32;
                        words[*regularCompressedPosition + 13] = 33;
                        words[*regularCompressedPosition + 21] = 34;
                        words[*regularCompressedPosition + 29] = 35;
                    }
                }
            } else {
                if (mode == CompositePatchMode::rawSslrIsolation) {
                    words.erase(
                        words.begin() + *conditionalOutputPosition,
                        words.begin() + *conditionalOutputPosition +
                            kStockConditionalFinalOutput.size());
                    words.insert(
                        words.begin() + *conditionalOutputPosition,
                        kConditionalRawSslrFinalOutput.begin(),
                        kConditionalRawSslrFinalOutput.end());
                } else {
                    if (mode == CompositePatchMode::
                            rawStockCubemapIsolation) {
                        words.erase(
                            words.begin() + *conditionalOutputPosition,
                            words.begin() + *conditionalOutputPosition +
                                kStockConditionalFinalOutput.size());
                        words.insert(
                            words.begin() + *conditionalOutputPosition,
                            kConditionalRawCubemapFinalOutput.begin(),
                            kConditionalRawCubemapFinalOutput.end());
                        words.erase(
                            words.begin() + *conditionalSamplePosition,
                            words.begin() + *conditionalSamplePosition +
                                kConditionalSslrSample.size());
                        words.insert(
                            words.begin() + *conditionalSamplePosition,
                            kConditionalCubemapStage.begin(),
                            kConditionalCubemapStage.end());
                    }
                    if (mode == CompositePatchMode::surfaceAnchoredCubemap) {
                        words.insert(
                            words.begin() + *conditionalReconstructionPosition +
                                kStockConditionalPositionReconstruction.size(),
                            kConditionalSurfaceAnchorCorrection.begin(),
                            kConditionalSurfaceAnchorCorrection.end());
                        words.insert(
                            words.begin() + *conditionalReconstructionPosition,
                            kConditionalSurfaceRaySetup.begin(),
                            kConditionalSurfaceRaySetup.end());
                        words[*conditionalCompressedPosition + 5] = 32;
                        words[*conditionalCompressedPosition + 13] = 33;
                        words[*conditionalCompressedPosition + 21] = 34;
                        words[*conditionalCompressedPosition + 29] = 35;
                    }
                }
            }
            if (words.size() >
                (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            words[1] = static_cast<std::uint32_t>(words.size());

            const auto newShaderSize = words.size() * sizeof(std::uint32_t);
            const auto oldShaderSize = static_cast<std::size_t>(
                shaderChunk.size);
            const auto fixedContainerSize =
                stockBytecode.size() - oldShaderSize;
            if (newShaderSize >
                (std::numeric_limits<std::size_t>::max)() -
                    fixedContainerSize) {
                return false;
            }
            const auto newContainerSize = fixedContainerSize + newShaderSize;
            if (newContainerSize >
                (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            const auto oldShaderEnd = shaderPayloadOffset + shaderChunk.size;
            std::vector<std::byte> candidate;
            candidate.reserve(newContainerSize);
            candidate.insert(
                candidate.end(),
                stockBytecode.begin(),
                stockBytecode.begin() + shaderPayloadOffset);
            const auto* newShaderBytes = reinterpret_cast<const std::byte*>(
                words.data());
            candidate.insert(
                candidate.end(),
                newShaderBytes,
                newShaderBytes + newShaderSize);
            candidate.insert(
                candidate.end(),
                stockBytecode.begin() + oldShaderEnd,
                stockBytecode.end());

            if (candidate.size() != newContainerSize ||
                !writeU32(
                    candidate,
                    kDeclaredSizeOffset,
                    static_cast<std::uint32_t>(candidate.size())) ||
                !writeU32(
                    candidate,
                    static_cast<std::size_t>(shaderChunk.offset) + 4,
                    static_cast<std::uint32_t>(newShaderSize))) {
                return false;
            }
            for (std::size_t index = 0; index < chunks.size(); ++index) {
                if (chunks[index].offset <= shaderChunk.offset) {
                    continue;
                }
                std::size_t adjustedOffset{};
                if (newShaderSize >= oldShaderSize) {
                    const auto growth = newShaderSize - oldShaderSize;
                    if (chunks[index].offset >
                        (std::numeric_limits<std::uint32_t>::max)() - growth) {
                        return false;
                    }
                    adjustedOffset = chunks[index].offset + growth;
                } else {
                    const auto shrink = oldShaderSize - newShaderSize;
                    if (chunks[index].offset < shrink) {
                        return false;
                    }
                    adjustedOffset = chunks[index].offset - shrink;
                }
                if (!writeU32(
                        candidate,
                        kChunkOffsetsOffset +
                            index * sizeof(std::uint32_t),
                        static_cast<std::uint32_t>(adjustedOffset))) {
                    return false;
                }
            }
            if (!linear_lighting::recomputeDxbcChecksum(candidate)) {
                return false;
            }
            patchedBytecode.swap(candidate);
            return true;
        } catch (...) {
            patchedBytecode.clear();
            return false;
        }
        }
    }

    bool patchStockReflectionCompositeSurfaceAnchoredCubemap(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        return patchStockReflectionComposite(
            stockBytecode,
            patchedBytecode,
            CompositePatchMode::surfaceAnchoredCubemap);
    }

    bool patchStockReflectionCompositeRawSslr(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        return patchStockReflectionComposite(
            stockBytecode,
            patchedBytecode,
            CompositePatchMode::rawSslrIsolation);
    }

    bool patchStockReflectionCompositeRawStockCubemap(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        return patchStockReflectionComposite(
            stockBytecode,
            patchedBytecode,
            CompositePatchMode::rawStockCubemapIsolation);
    }
}
