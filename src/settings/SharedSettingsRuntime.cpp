#include "settings/SharedSettingsRuntime.h"

#include "Features/basic_wetness/BasicWetnessSettingsStore.h"
#include "Features/cloud_shadows/CloudShadowSettingsStore.h"
#include "Features/complex_materials/ComplexParallaxSettingsStore.h"
#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/DlaaSettingsStore.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/ibl/IblSettingsStore.h"
#include "Features/hair_specular/HairSpecularSettingsStore.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/wrapped_grass/WrappedGrassSettingsStore.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"
#include "ui/WristPanelRuntime.h"
#include "ui/WristPanelSettings.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace community_shaders::shared_settings
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr auto kPollInterval = 200ms;
        constexpr std::uintmax_t kMaximumIniBytes = 1024u * 1024u;
        constexpr std::uint32_t kRequiredStableObservations = 2;

        struct FileSignature final
        {
            std::filesystem::file_time_type writeTime{};
            std::uintmax_t size{};

            [[nodiscard]] bool operator==(
                const FileSignature&) const noexcept = default;
        };

        [[nodiscard]] std::optional<FileSignature> readSignature(
            const std::filesystem::path& path) noexcept
        {
            std::error_code regularError;
            if (path.empty() ||
                !std::filesystem::is_regular_file(path, regularError) ||
                regularError) {
                return std::nullopt;
            }

            std::error_code timeError;
            std::error_code sizeError;
            const auto writeTime =
                std::filesystem::last_write_time(path, timeError);
            const auto size = std::filesystem::file_size(path, sizeError);
            if (timeError || sizeError || size > kMaximumIniBytes) {
                return std::nullopt;
            }
            return FileSignature{ .writeTime = writeTime, .size = size };
        }

        [[nodiscard]] Snapshot loadSnapshot(
            const std::filesystem::path& path,
            const Snapshot& fallback) noexcept
        {
            Snapshot result{
                .linearLighting = linear_lighting::loadSettings(path),
                .dlaa = dlaa::loadSettings(path),
                .ibl = ibl::loadSettings(path),
                .complexMaterials =
                    complex_materials::loadSettings(path),
                .contactShadows = contact_shadows::loadSettings(path),
                .wrappedGrass = wrapped_grass::loadSettings(path),
                .hairSpecular = hair_specular::loadSettings(path),
                .subsurfaceScattering =
                    subsurface_scattering::loadSettings(path),
                .basicWetness = basic_wetness::loadSettings(path),
                .cloudShadows = cloud_shadows::loadSettings(path),
                .vanillaFixes = vanilla_fixes::loadSettings(path),
                .nativeShadows = native_shadows::loadSettings(path),
                .prismaPanelEnabled = fallback.prismaPanelEnabled,
            };
            const auto panel = ui::wrist_panel_settings::load(path);
            if (panel.valueValid) {
                result.prismaPanelEnabled = panel.enabled;
            }
            return result;
        }

        void applyLiveChanges(
            const ChangeSet& changes,
            const Snapshot& next) noexcept
        {
            if (changes.linearLighting) {
                linear_lighting::Runtime::get().queueSettings(
                    next.linearLighting);
                ui::setInitialSettings(next.linearLighting);
            }
            if (changes.dlaa) {
                dlaa::Runtime::get().applySettings(next.dlaa);
            }
            if (changes.ibl) {
                ibl::Runtime::get().applySettings(next.ibl);
            }
            if (changes.complexMaterials) {
                linear_lighting::Runtime::get().queueComplexParallaxSettings(
                    next.complexMaterials);
                ui::setInitialComplexParallaxSettings(
                    next.complexMaterials);
            }
            if (changes.contactShadows) {
                contact_shadows::Runtime::get().applySettings(
                    next.contactShadows);
            }
            if (changes.wrappedGrass) {
                ui::setInitialWrappedGrassSettings(next.wrappedGrass);
            }
            if (changes.hairSpecular) {
                ui::setInitialHairSpecularSettings(next.hairSpecular);
            }
            if (changes.subsurfaceScattering) {
                ui::setInitialSubsurfaceScatteringSettings(
                    next.subsurfaceScattering);
            }
            if (changes.basicWetness) {
                ui::setInitialBasicWetnessSettings(next.basicWetness);
            }
            if (changes.cloudShadows) {
                ui::setInitialCloudShadowSettings(next.cloudShadows);
            }
            if (changes.vanillaFixes) {
                ui::setInitialVanillaFixesSettings(next.vanillaFixes);
            }
            if (changes.prismaPanel) {
                ui::schedulePrismaPanelSettingReload();
            }
            if (changes.liveFeatureCount() != 0) {
                ui::notifyExternalSettingsReload();
            }
        }

        class Controller final
        {
        public:
            [[nodiscard]] static Controller& get() noexcept
            {
                static auto* controller = new Controller();
                return *controller;
            }

            [[nodiscard]] bool start() noexcept
            {
                auto expected = false;
                if (!started_.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    return true;
                }

                configPath_ = settings_path::resolveIniPath();
                if (configPath_.empty()) {
                    started_.store(false, std::memory_order_release);
                    return false;
                }
                active_ = loadSnapshot(configPath_, Snapshot{});
                accepted_ = readSignature(configPath_);

                try {
                    monitor_ = std::jthread(
                        [this](const std::stop_token stopToken) noexcept {
                            monitorMain(stopToken);
                        });
                } catch (const std::exception& error) {
                    logging::warn(
                        "Shared settings monitor could not start: {}.",
                        error.what());
                    started_.store(false, std::memory_order_release);
                    return false;
                } catch (...) {
                    logging::warn(
                        "Shared settings monitor could not start.");
                    started_.store(false, std::memory_order_release);
                    return false;
                }

                logging::info(
                    "Shared Community Shaders INI monitor started for '{}'; DevMenu remains optional and Native Shadows changes remain restart-only.",
                    configPath_.string());
                return true;
            }

        private:
            Controller() = default;

            void monitorMain(const std::stop_token stopToken) noexcept
            {
                std::unique_lock lock(wakeMutex_);
                while (!stopToken.stop_requested()) {
                    wake_.wait_for(
                        lock,
                        stopToken,
                        kPollInterval,
                        []() noexcept { return false; });
                    if (stopToken.stop_requested()) {
                        break;
                    }
                    lock.unlock();
                    reloadIfStable();
                    lock.lock();
                }
            }

            void reloadIfStable() noexcept
            {
                try {
                    const auto observed = readSignature(configPath_);
                    if (!observed) {
                        pending_.reset();
                        pendingObservations_ = 0;
                        return;
                    }
                    if (accepted_ && *accepted_ == *observed) {
                        pending_.reset();
                        pendingObservations_ = 0;
                        return;
                    }
                    if (!pending_ || *pending_ != *observed) {
                        pending_ = observed;
                        pendingObservations_ = 1;
                        return;
                    }
                    ++pendingObservations_;
                    if (pendingObservations_ < kRequiredStableObservations) {
                        return;
                    }

                    const auto next = loadSnapshot(configPath_, active_);
                    const auto stable = readSignature(configPath_);
                    if (!stable || *stable != *observed) {
                        pending_ = stable;
                        pendingObservations_ = stable ? 1u : 0u;
                        return;
                    }

                    accepted_ = stable;
                    pending_.reset();
                    pendingObservations_ = 0;
                    const auto changes = diff(active_, next);
                    if (!changes.any()) {
                        return;
                    }

                    applyLiveChanges(changes, next);
                    active_ = next;
                    const auto reloadNumber =
                        acceptedReloads_.fetch_add(
                            1,
                            std::memory_order_relaxed) +
                        1;
                    logging::info(
                        "Shared Community Shaders INI reload #{} accepted: live feature groups={}, Native Shadows restart pending={}, original Prisma panel changed={}.",
                        reloadNumber,
                        changes.liveFeatureCount(),
                        changes.nativeShadows,
                        changes.prismaPanel);
                } catch (const std::exception& error) {
                    logging::warn(
                        "Shared settings monitor rejected an INI reload: {}.",
                        error.what());
                } catch (...) {
                    logging::warn(
                        "Shared settings monitor rejected an INI reload.");
                }
            }

            std::atomic_bool started_{};
            std::filesystem::path configPath_{};
            Snapshot active_{};
            std::optional<FileSignature> accepted_{};
            std::optional<FileSignature> pending_{};
            std::uint32_t pendingObservations_{};
            std::atomic_uint64_t acceptedReloads_{};
            std::mutex wakeMutex_{};
            std::condition_variable_any wake_{};
            std::jthread monitor_{};
        };
    }

    bool startMonitor() noexcept
    {
        return Controller::get().start();
    }
}
