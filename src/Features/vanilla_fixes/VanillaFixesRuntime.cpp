#include "Features/vanilla_fixes/VanillaFixesRuntime.h"

#include "Features/vanilla_fixes/SslrEnvironmentBinding.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/vanilla_fixes/VanillaShaderFixes.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr auto kPollInterval = std::chrono::milliseconds(250);
        constexpr std::uintptr_t kIniSettingVtableRva = 0x02C81230;
        constexpr std::uintptr_t kIniPrefSettingVtableRva = 0x02C8C1B0;
        constexpr std::uintptr_t kRendererConfigRva = 0x068787F0;
        constexpr std::uintptr_t kImageSpaceManagerPointerRva = 0x068789E8;
        constexpr std::uintptr_t kSunbeamsAvailabilityRva = 0x0689AC94;
        constexpr std::uintptr_t kSaoEffectVtableRva = 0x030B8FD8;

        enum class PolicyField : std::uint8_t
        {
            kPrecipitationOcclusion,
            kImageSpaceModifiers,
            kSao,
            kScreenSpaceReflections,
            kScreenSpaceSubsurfaceScattering,
            kLensFlare,
            kFocusShadows,
            kSunbeams
        };

        struct SettingDescriptor final
        {
            const char* name;
            std::uintptr_t recordRva;
            std::uintptr_t nameRva;
            std::uintptr_t vtableRva;
            PolicyField field;
        };

        constexpr std::array kSettings{
            SettingDescriptor{
                "bPrecipitationOcclusion:VRDisplay",
                0x03740E38,
                0x02CF4248,
                kIniPrefSettingVtableRva,
                PolicyField::kPrecipitationOcclusion },
            SettingDescriptor{
                "bVrAllowFocusShadows:VRDisplay",
                0x037C69F8,
                0x02D78D48,
                kIniSettingVtableRva,
                PolicyField::kFocusShadows },
            SettingDescriptor{
                "bAllowImageSpaceModifiers:VRDisplay",
                0x037C6A10,
                0x02D78D68,
                kIniSettingVtableRva,
                PolicyField::kImageSpaceModifiers },
            SettingDescriptor{
                "bUseSunbeams:Display",
                0x037C76E8,
                0x02D7A3C0,
                kIniSettingVtableRva,
                PolicyField::kSunbeams },
            SettingDescriptor{
                "bVrAllowSAO:VRDisplay",
                0x03924D50,
                0x030B9658,
                kIniSettingVtableRva,
                PolicyField::kSao },
            SettingDescriptor{
                "bSAOEnable:Display",
                0x03924D68,
                0x030B9670,
                kIniPrefSettingVtableRva,
                PolicyField::kSao },
            SettingDescriptor{
                "bLensFlareVr:VrDisplay",
                0x03924EB8,
                0x030B9810,
                kIniSettingVtableRva,
                PolicyField::kLensFlare },
            SettingDescriptor{
                "bLensFlare:ImageSpace",
                0x03924ED0,
                0x030B9828,
                kIniPrefSettingVtableRva,
                PolicyField::kLensFlare },
            SettingDescriptor{
                "bVrAllowScreenSpaceReflections:VRDisplay",
                0x039255F0,
                0x030BA1F0,
                kIniSettingVtableRva,
                PolicyField::kScreenSpaceReflections },
            SettingDescriptor{
                "bVrAllowScreenSpaceSubsurfaceScattering:VRDisplay",
                0x03925608,
                0x030BA220,
                kIniSettingVtableRva,
                PolicyField::kScreenSpaceSubsurfaceScattering },
        };

        [[nodiscard]] bool requestedValue(
            const Settings& policy,
            const PolicyField field) noexcept
        {
            if (!policy.enabled) {
                return false;
            }
            switch (field) {
            case PolicyField::kPrecipitationOcclusion:
                return policy.precipitationOcclusion;
            case PolicyField::kImageSpaceModifiers:
                return policy.imageSpaceModifiers;
            case PolicyField::kSao:
                return policy.sao;
            case PolicyField::kScreenSpaceReflections:
                return policy.screenSpaceReflections;
            case PolicyField::kScreenSpaceSubsurfaceScattering:
                return policy.screenSpaceSubsurfaceScattering;
            case PolicyField::kLensFlare:
                return policy.lensFlare;
            case PolicyField::kFocusShadows:
                return policy.focusShadows;
            case PolicyField::kSunbeams:
                return policy.sunbeams;
            }
            return false;
        }

        [[nodiscard]] Settings effectivePolicy(Settings policy) noexcept
        {
            if (policy.directionalLightDiagnosticMode ==
                DirectionalLightDiagnosticMode::off) {
                return policy;
            }
            policy.precipitationOcclusion = false;
            policy.imageSpaceModifiers = false;
            policy.sao = false;
            policy.screenSpaceReflections = false;
            policy.screenSpaceSubsurfaceScattering = false;
            policy.lensFlare = false;
            policy.focusShadows = false;
            policy.sunbeams = false;
            return policy;
        }

        [[nodiscard]] bool readableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            const auto regionEnd = regionBegin + information.RegionSize;
            return begin >= regionBegin && begin <= regionEnd &&
                size <= regionEnd - begin;
        }

        [[nodiscard]] bool writableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!readableRange(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            (void)VirtualQuery(address, &information, sizeof(information));
            constexpr DWORD writableMask = PAGE_READWRITE |
                PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & writableMask) != 0;
        }

        void writeBoolean(std::uint8_t* destination, const bool value) noexcept
        {
            (void)InterlockedExchange8(
                reinterpret_cast<volatile char*>(destination),
                value ? 1 : 0);
        }


        class Controller final
        {
        public:
            [[nodiscard]] static Controller& get() noexcept
            {
                static auto* controller = new Controller();
                return *controller;
            }

            [[nodiscard]] bool start(const Settings& settings) noexcept
            {
                auto expected = false;
                if (!started_.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    apply(settings);
                    return contractValid_;
                }
                moduleBase_ = reinterpret_cast<std::uintptr_t>(
                    GetModuleHandleW(nullptr));
                if (!validateContract()) {
                    started_.store(false, std::memory_order_release);
                    return false;
                }
                configPath_ = settings_path::resolveIniPath();
                apply(settings);
                (void)saveSettings(settings);
                refreshAcceptedWriteTime();
                try {
                    monitor_ = std::jthread(
                        [this](const std::stop_token stopToken) noexcept {
                            monitorMain(stopToken);
                        });
                    hotReloadActive_.store(true, std::memory_order_release);
                } catch (const std::exception& error) {
                    logging::warn(
                        "Vanilla Fixes INI monitor could not start: {}.",
                        error.what());
                } catch (...) {
                    logging::warn(
                        "Vanilla Fixes INI monitor could not start.");
                }
                logging::info(
                    "Vanilla Fixes owns 8 verified engine gates, the coordinated stable-reflection suite, and the isolated directional-light ownership diagnostic; shared-INI hot reload active={}.",
                    hotReloadActive_.load(std::memory_order_acquire));
                return true;
            }

            void apply(const Settings& settings) noexcept
            {
                {
                    std::scoped_lock lock(settingsMutex_);
                    activeSettings_ = settings;
                    applyMemoryPolicy(effectivePolicy(activeSettings_));
                }
                focusEnabled_.store(
                    settings.enabled && settings.focusShadows &&
                        settings.directionalLightDiagnosticMode ==
                            DirectionalLightDiagnosticMode::off,
                    std::memory_order_release);
                setSslrSuiteRequested(
                    settings.enabled && settings.screenSpaceReflections &&
                    settings.directionalLightDiagnosticMode ==
                        DirectionalLightDiagnosticMode::off);
                const auto diagnosticMode = settings.enabled ?
                    settings.directionalLightDiagnosticMode :
                    DirectionalLightDiagnosticMode::off;
                diagnosticMode_.store(
                    static_cast<std::uint8_t>(diagnosticMode),
                    std::memory_order_release);
                setDirectionalLightDiagnosticMode(diagnosticMode);
                appliedPolicies_.fetch_add(1, std::memory_order_relaxed);
            }

            [[nodiscard]] Settings settings() const noexcept
            {
                std::scoped_lock lock(settingsMutex_);
                return activeSettings_;
            }

            [[nodiscard]] RuntimeSnapshot snapshot() const noexcept
            {
                return {
                    .settings = settings(),
                    .nativeContractValid = contractValid_,
                    .hotReloadActive =
                        hotReloadActive_.load(std::memory_order_acquire),
                    .appliedPolicies =
                        appliedPolicies_.load(std::memory_order_relaxed),
                    .externalReloads =
                        externalReloads_.load(std::memory_order_relaxed),
                };
            }

            [[nodiscard]] bool focusEnabled() const noexcept
            {
                return focusEnabled_.load(std::memory_order_acquire);
            }

            [[nodiscard]] DirectionalLightDiagnosticMode diagnosticMode()
                const noexcept
            {
                return static_cast<DirectionalLightDiagnosticMode>(
                    diagnosticMode_.load(std::memory_order_acquire));
            }

        private:
            Controller() = default;

            void refreshAcceptedWriteTime() noexcept
            {
                if (configPath_.empty()) {
                    return;
                }
                std::error_code error;
                const auto value = std::filesystem::last_write_time(
                    configPath_,
                    error);
                if (!error) {
                    acceptedWriteTime_ = value;
                }
            }

            void monitorMain(const std::stop_token stopToken) noexcept
            {
                std::unique_lock lock(monitorMutex_);
                while (!stopToken.stop_requested()) {
                    monitorWake_.wait_for(
                        lock,
                        stopToken,
                        kPollInterval,
                        []() noexcept { return false; });
                    if (stopToken.stop_requested()) {
                        break;
                    }
                    lock.unlock();
                    reloadIfChanged();
                    {
                        std::scoped_lock settingsLock(settingsMutex_);
                        applyMemoryPolicy(effectivePolicy(activeSettings_));
                    }
                    lock.lock();
                }
            }

            void reloadIfChanged() noexcept
            {
                try {
                    if (configPath_.empty()) {
                        return;
                    }
                    std::error_code timeError;
                    std::error_code sizeError;
                    const auto beforeTime = std::filesystem::last_write_time(
                        configPath_,
                        timeError);
                    const auto beforeSize = std::filesystem::file_size(
                        configPath_,
                        sizeError);
                    if (timeError || sizeError || beforeSize > 1024 * 1024 ||
                        (acceptedWriteTime_ &&
                            *acceptedWriteTime_ == beforeTime)) {
                        return;
                    }
                    const auto reloaded = loadSettings(configPath_);
                    std::error_code stableTimeError;
                    std::error_code stableSizeError;
                    const auto stableTime = std::filesystem::last_write_time(
                        configPath_,
                        stableTimeError);
                    const auto stableSize = std::filesystem::file_size(
                        configPath_,
                        stableSizeError);
                    if (stableTimeError || stableSizeError ||
                        stableTime != beforeTime || stableSize != beforeSize) {
                        return;
                    }
                    acceptedWriteTime_ = stableTime;
                    if (reloaded == settings()) {
                        return;
                    }
                    apply(reloaded);
                    externalReloads_.fetch_add(1, std::memory_order_relaxed);
                    logging::info(
                        "Vanilla Fixes reloaded the shared INI in-game.");
                } catch (const std::exception& error) {
                    logging::warn(
                        "Vanilla Fixes rejected an INI reload: {}.",
                        error.what());
                } catch (...) {
                    logging::warn(
                        "Vanilla Fixes rejected an INI reload.");
                }
            }

            [[nodiscard]] bool validateRipTarget(
                const std::uintptr_t instructionRva,
                const std::span<const std::uint8_t> opcode,
                const std::size_t displacementOffset,
                const std::size_t instructionLength,
                const std::uintptr_t expectedTargetRva) const noexcept
            {
                const auto* instruction = reinterpret_cast<const std::uint8_t*>(
                    moduleBase_ + instructionRva);
                if (!readableRange(instruction, instructionLength) ||
                    opcode.size() > instructionLength ||
                    std::memcmp(instruction, opcode.data(), opcode.size()) != 0 ||
                    displacementOffset + sizeof(std::int32_t) >
                        instructionLength) {
                    return false;
                }
                std::int32_t displacement{};
                std::memcpy(
                    &displacement,
                    instruction + displacementOffset,
                    sizeof(displacement));
                const auto target = moduleBase_ + instructionRva +
                    instructionLength + displacement;
                return target == moduleBase_ + expectedTargetRva;
            }

            [[nodiscard]] bool validateContract() noexcept
            {
                if (!moduleBase_) {
                    logging::critical(
                        "Visual gate contract rejected: Fallout4VR module base is unavailable.");
                    return false;
                }
                for (const auto& setting : kSettings) {
                    const auto* record = reinterpret_cast<const std::uint8_t*>(
                        moduleBase_ + setting.recordRva);
                    if (!readableRange(record, 0x18) ||
                        !writableRange(record + 8, 1)) {
                        logging::critical(
                            "Visual gate contract rejected: setting record '{}' is inaccessible.",
                            setting.name);
                        return false;
                    }
                    std::uintptr_t vtable{};
                    std::uintptr_t namePointer{};
                    std::memcpy(&vtable, record, sizeof(vtable));
                    std::memcpy(
                        &namePointer,
                        record + 0x10,
                        sizeof(namePointer));
                    const auto expectedNameSize =
                        std::strlen(setting.name) + 1;
                    if (vtable != moduleBase_ + setting.vtableRva ||
                        namePointer != moduleBase_ + setting.nameRva ||
                        !readableRange(
                            reinterpret_cast<const void*>(namePointer),
                            expectedNameSize) ||
                        std::memcmp(
                            reinterpret_cast<const char*>(namePointer),
                            setting.name,
                            expectedNameSize) != 0 ||
                        record[8] > 1) {
                        logging::critical(
                            "Visual gate contract rejected: setting record '{}' failed identity validation.",
                            setting.name);
                        return false;
                    }
                }

                constexpr std::array<std::uint8_t, 2> compareRip{ 0x80, 0x3D };
                constexpr std::array<std::uint8_t, 3> loadRip{
                    0x0F, 0xB6, 0x05 };
                constexpr std::array<std::uint8_t, 3> leaRip{
                    0x48, 0x8D, 0x05 };
                constexpr std::array<std::uint8_t, 3> storeRip{
                    0x48, 0x89, 0x05 };
                if (!validateRipTarget(
                        0x0288D68F, compareRip, 2, 7, 0x03924D58) ||
                    !validateRipTarget(
                        0x0288D69D, compareRip, 2, 7, 0x03924D70) ||
                    !validateRipTarget(
                        0x0288D953, compareRip, 2, 7, 0x03924EC0) ||
                    !validateRipTarget(
                        0x0288D95C, compareRip, 2, 7, 0x03924ED8) ||
                    !validateRipTarget(
                        0x0288DA2E, loadRip, 3, 7, 0x039255F8) ||
                    !validateRipTarget(
                        0x0288DA3B, loadRip, 3, 7, 0x03925610) ||
                    !validateRipTarget(
                        0x027AEEB0, leaRip, 3, 7, kRendererConfigRva) ||
                    !validateRipTarget(
                        0x027F4F0A,
                        storeRip,
                        3,
                        7,
                        kImageSpaceManagerPointerRva) ||
                    !validateRipTarget(
                        0x0288D156,
                        compareRip,
                        2,
                        7,
                        kSunbeamsAvailabilityRva)) {
                    logging::critical(
                        "Visual gate contract rejected: Fallout4VR consumer signatures do not match 1.2.72.0.");
                    return false;
                }

                constexpr std::array<std::uint8_t, 3> saoStore{
                    0x88, 0x42, 0x32 };
                constexpr std::array<std::uint8_t, 6> lensStore{
                    0x88, 0x8A, 0xEC, 0x00, 0x00, 0x00 };
                constexpr std::array<std::uint8_t, 6> ssrStore{
                    0x88, 0x82, 0x1C, 0x01, 0x00, 0x00 };
                constexpr std::array<std::uint8_t, 6> sssStore{
                    0x88, 0x82, 0x1D, 0x01, 0x00, 0x00 };
                const auto exact = [this](
                                       const std::uintptr_t rva,
                                       const auto& expected) noexcept {
                    const auto* address = reinterpret_cast<const std::uint8_t*>(
                        moduleBase_ + rva);
                    return readableRange(address, expected.size()) &&
                        std::memcmp(
                            address,
                            expected.data(),
                            expected.size()) == 0;
                };
                if (!exact(0x0288D6AC, saoStore) ||
                    !exact(0x0288D96F, lensStore) ||
                    !exact(0x0288DA35, ssrStore) ||
                    !exact(0x0288DA42, sssStore) ||
                    !writableRange(
                        reinterpret_cast<void*>(
                            moduleBase_ + kRendererConfigRva + 0x32),
                        1) ||
                    !writableRange(
                        reinterpret_cast<void*>(
                            moduleBase_ + kSunbeamsAvailabilityRva),
                        1)) {
                    logging::critical(
                        "Visual gate contract rejected: effective runtime targets failed validation.");
                    return false;
                }

                contractValid_ = true;
                logging::info(
                    "Visual gate memory contract passed: 10 setting records, renderer snapshot, SAO instance path, and sunbeams availability are verified.");
                return true;
            }

            void applyMemoryPolicy(const Settings& policy) noexcept
            {
                if (!contractValid_) {
                    return;
                }
                for (const auto& setting : kSettings) {
                    const auto preserveStartupCapability =
                        setting.field == PolicyField::kSao ||
                        setting.field == PolicyField::kScreenSpaceReflections ||
                        setting.field ==
                            PolicyField::kScreenSpaceSubsurfaceScattering ||
                        setting.field == PolicyField::kLensFlare ||
                        setting.field == PolicyField::kSunbeams;
                    writeBoolean(
                        reinterpret_cast<std::uint8_t*>(
                            moduleBase_ + setting.recordRva + 8),
                        preserveStartupCapability ? true :
                            requestedValue(policy, setting.field));
                }

                auto* renderer = reinterpret_cast<std::uint8_t*>(
                    moduleBase_ + kRendererConfigRva);
                const auto sao =
                    requestedValue(policy, PolicyField::kSao);
                const auto lensFlare =
                    requestedValue(policy, PolicyField::kLensFlare);
                const auto screenSpaceReflections = requestedValue(
                    policy,
                    PolicyField::kScreenSpaceReflections);
                const auto screenSpaceSubsurfaceScattering = requestedValue(
                    policy,
                    PolicyField::kScreenSpaceSubsurfaceScattering);
                const auto sunbeams =
                    requestedValue(policy, PolicyField::kSunbeams);
                writeBoolean(renderer + 0x32, sao);
                writeBoolean(renderer + 0xEC, lensFlare);
                writeBoolean(
                    renderer + 0x11C,
                    screenSpaceReflections);
                writeBoolean(
                    renderer + 0x11D,
                    screenSpaceSubsurfaceScattering);
                writeBoolean(
                    reinterpret_cast<std::uint8_t*>(
                        moduleBase_ + kSunbeamsAvailabilityRva),
                    sunbeams);
                refreshSaoEffect(sao);
            }

            void refreshSaoEffect(const bool enabled) noexcept
            {
                const auto* managerPointer = reinterpret_cast<
                    const std::uintptr_t*>(
                    moduleBase_ + kImageSpaceManagerPointerRva);
                if (!readableRange(managerPointer, sizeof(*managerPointer))) {
                    return;
                }
                std::uintptr_t manager{};
                std::memcpy(&manager, managerPointer, sizeof(manager));
                if (!manager || !readableRange(
                        reinterpret_cast<const void*>(manager), 0xBD)) {
                    return;
                }
                if (*reinterpret_cast<const std::uint8_t*>(manager + 0xBC) ==
                    0) {
                    return;
                }
                std::uintptr_t effects{};
                std::memcpy(
                    &effects,
                    reinterpret_cast<const void*>(manager + 0x18),
                    sizeof(effects));
                // The factory grows the array to count 0x48, then stores the
                // CS SAO object at byte offset 0x238: zero-based slot 0x47.
                constexpr std::size_t saoEffectIndex = 0x47;
                if (!effects || !readableRange(
                        reinterpret_cast<const void*>(
                            effects + saoEffectIndex * sizeof(std::uintptr_t)),
                        sizeof(std::uintptr_t))) {
                    return;
                }
                std::uintptr_t effect{};
                std::memcpy(
                    &effect,
                    reinterpret_cast<const void*>(
                        effects + saoEffectIndex * sizeof(std::uintptr_t)),
                    sizeof(effect));
                if (!effect || !readableRange(
                        reinterpret_cast<const void*>(effect), 0x121) ||
                    !writableRange(
                        reinterpret_cast<const void*>(effect + 8), 1)) {
                    return;
                }
                std::uintptr_t vtable{};
                std::memcpy(
                    &vtable,
                    reinterpret_cast<const void*>(effect),
                    sizeof(vtable));
                if (vtable != moduleBase_ + kSaoEffectVtableRva) {
                    if (!saoIdentityFailureLogged_) {
                        saoIdentityFailureLogged_ = true;
                        logging::critical(
                            "SAO live apply rejected: effect slot 0x47 has an unexpected vtable; source and renderer gates remain controlled.");
                    }
                    return;
                }
                writeBoolean(
                    reinterpret_cast<std::uint8_t*>(effect + 8),
                    enabled);
            }


            std::uintptr_t moduleBase_{};
            std::filesystem::path configPath_;
            std::optional<std::filesystem::file_time_type> acceptedWriteTime_;
            Settings activeSettings_{};
            bool contractValid_{};
            bool saoIdentityFailureLogged_{};
            std::atomic_bool started_{};
            std::atomic_bool hotReloadActive_{};
            std::atomic_bool focusEnabled_{ true };
            std::atomic_uint8_t diagnosticMode_{};
            std::atomic_uint64_t appliedPolicies_{};
            std::atomic_uint64_t externalReloads_{};
            mutable std::mutex settingsMutex_;
            std::mutex monitorMutex_;
            std::condition_variable_any monitorWake_;
            std::jthread monitor_;
        };
    }

    bool startRuntime(const Settings& settings) noexcept
    {
        return Controller::get().start(settings);
    }

    void applySettings(const Settings& settings) noexcept
    {
        Controller::get().apply(settings);
    }

    Settings activeSettings() noexcept
    {
        return Controller::get().settings();
    }

    bool focusShadowsEnabled() noexcept
    {
        return Controller::get().focusEnabled();
    }

    DirectionalLightDiagnosticMode directionalLightDiagnosticMode() noexcept
    {
        return Controller::get().diagnosticMode();
    }

    RuntimeSnapshot runtimeSnapshot() noexcept
    {
        return Controller::get().snapshot();
    }
}
