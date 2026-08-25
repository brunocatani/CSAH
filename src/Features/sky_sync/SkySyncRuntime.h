#pragma once

#include "Features/sky_sync/SkySyncSettings.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace RE
{
    class Sky;
}

namespace community_shaders::sky_sync
{
    enum class CelestialSource : std::uint32_t
    {
        none,
        sun,
        moon,
    };

    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool hookInstalled{};
        bool hookOwned{};
        bool stereoCameraReady{};
        bool sunValid{};
        bool moonValid{};
        bool applied{};
        CelestialSource source{ CelestialSource::none };
        std::uint32_t moonPhase{};
        std::array<float, 3> sunDirection{};
        std::array<float, 3> moonDirection{};
        std::array<float, 3> appliedDirection{};
        std::array<float, 3> moonColor{};
        std::uint64_t skyUpdates{};
        std::uint64_t directionApplications{};
        std::uint64_t rejectedFrames{};
        std::uint64_t sourceTransitions{};
    };

    class Runtime final
    {
    public:
        [[nodiscard]] static Runtime& get() noexcept;

        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] bool installHook() noexcept;
        [[nodiscard]] bool validateHook(const char* trigger) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

        void onSkyUpdated(RE::Sky* sky, float deltaSeconds) noexcept;

    private:
        Runtime() = default;

        std::atomic_bool enabled_{ true };
        std::atomic_bool hookInstalled_{};
        std::atomic_bool hookOwned_{};
        std::atomic_bool stereoCameraReady_{};
        std::atomic_bool sunValid_{};
        std::atomic_bool moonValid_{};
        std::atomic_bool applied_{};
        std::atomic_uint32_t source_{};
        std::atomic_uint32_t moonPhase_{};
        std::array<std::atomic_uint32_t, 3> sunDirectionBits_{};
        std::array<std::atomic_uint32_t, 3> moonDirectionBits_{};
        std::array<std::atomic_uint32_t, 3> appliedDirectionBits_{};
        std::array<std::atomic_uint32_t, 3> moonColorBits_{};
        std::array<float, 3> currentDirection_{};
        CelestialSource currentSource_{ CelestialSource::none };
        bool currentDirectionValid_{};
        std::atomic_uint64_t skyUpdates_{};
        std::atomic_uint64_t directionApplications_{};
        std::atomic_uint64_t rejectedFrames_{};
        std::atomic_uint64_t sourceTransitions_{};
    };
}
