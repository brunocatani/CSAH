#include "ui/WristPanelRuntime.h"

#include "PrismaUI_F4_API.h"
#include "PrismaUI_F4VR_API.h"
#include "ROCKProviderApi.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "render/BSLightingGeometryHook.h"
#include "render/D3D11Hooks.h"
#include "support/Logger.h"
#include "ui/PointerClickGate.h"

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace community_shaders::ui
{
    namespace
    {
        using rock::provider::RockProviderApi;
        using rock::provider::RockProviderConsumerCapabilityV1;
        using rock::provider::RockProviderConsumerHandleV1;
        using rock::provider::RockProviderConsumerRegistrationV1;
        using rock::provider::RockProviderDebugOverlayPublicationV1;
        using rock::provider::RockProviderDebugOverlayTextV1;
        using rock::provider::RockProviderFrameSnapshot;
        using rock::provider::RockProviderHand;
        using rock::provider::RockProviderHandInputSuppressionFlagV1;
        using rock::provider::RockProviderHandInputSuppressionRequestV1;
        using rock::provider::RockProviderLifecycleFlag;
        using rock::provider::RockProviderLimitsV1;
        using rock::provider::RockProviderRawWandButtonStateV1;
        using rock::provider::RockProviderResultV1;

        constexpr std::uint32_t kPanelWidthPixels = 1280;
        constexpr std::uint32_t kPanelHeightPixels = 900;
        constexpr float kPanelPhysicalWidth = 22.0f;
        constexpr float kPanelPhysicalHeight =
            kPanelPhysicalWidth * static_cast<float>(kPanelHeightPixels) /
            static_cast<float>(kPanelWidthPixels);
        constexpr float kPanelForward = 9.0f;
        constexpr float kPanelLateral = 0.0f;
        constexpr float kPanelUp = 7.0f;
        constexpr float kPointerMaxDistance = 500.0f;
        constexpr std::uint32_t kLeftTriggerButtonId = 33;
        constexpr std::uint32_t kLeftXButtonId = 7;
        constexpr std::uint32_t kSuppressionLeaseFrames = 3;
        constexpr std::uint64_t kRouteFreshnessFrames = 8;
        constexpr std::uint32_t kF4VrApiFlavor = 0x52563446u;
        constexpr std::uint64_t kRequiredSpatialFeatures =
            PRISMA_UI_VR_API::SpatialFeature_FullPose |
            PRISMA_UI_VR_API::SpatialFeature_IndependentDimensions |
            PRISMA_UI_VR_API::SpatialFeature_LatestOnlyUpdates |
            PRISMA_UI_VR_API::SpatialFeature_AppliedSequenceQuery |
            PRISMA_UI_VR_API::SpatialFeature_GpuRendering |
            PRISMA_UI_VR_API::SpatialFeature_NativeNetworkPolicy |
            PRISMA_UI_VR_API::SpatialFeature_SceneDepthOcclusion |
            PRISMA_UI_VR_API::SpatialFeature_WorldPointerInput |
            PRISMA_UI_VR_API::SpatialFeature_CentralPointerRouting;
        constexpr auto kModelPublishInterval =
            std::chrono::milliseconds(100);

        struct Vec3
        {
            float x{};
            float y{};
            float z{};
        };

        [[nodiscard]] Vec3 operator+(const Vec3& left, const Vec3& right) noexcept
        {
            return { left.x + right.x, left.y + right.y, left.z + right.z };
        }

        [[nodiscard]] Vec3 operator-(const Vec3& left, const Vec3& right) noexcept
        {
            return { left.x - right.x, left.y - right.y, left.z - right.z };
        }

        [[nodiscard]] Vec3 operator*(const Vec3& value, float scalar) noexcept
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        [[nodiscard]] bool finite(const Vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] float dot(const Vec3& left, const Vec3& right) noexcept
        {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        }

        [[nodiscard]] Vec3 cross(const Vec3& left, const Vec3& right) noexcept
        {
            return {
                left.y * right.z - left.z * right.y,
                left.z * right.x - left.x * right.z,
                left.x * right.y - left.y * right.x,
            };
        }

        [[nodiscard]] std::optional<Vec3> normalize(const Vec3& value) noexcept
        {
            if (!finite(value)) {
                return std::nullopt;
            }
            const auto lengthSquared = dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f) {
                return std::nullopt;
            }
            return value * (1.0f / std::sqrt(lengthSquared));
        }

        struct PanelPose
        {
            Vec3 position{};
            std::array<float, 4> orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
        };

        struct PointerRay
        {
            Vec3 origin{};
            Vec3 direction{};
        };

        struct FrameData
        {
            bool ready{};
            std::uint64_t frameIndex{};
            std::optional<PanelPose> panelPose{};
            std::optional<PointerRay> pointerRay{};
            bool pointerPrimaryDown{};
        };

        PRISMA_UI_API::IVPrismaUI4* prisma{};
        PRISMA_UI_VR_API::IVPrismaUIVR1* prismaVr{};
        PRISMA_UI_VR_API::SpatialCapabilitiesV1 spatialCapabilities{};
        PrismaView view{};
        std::atomic_bool initialized{};
        std::atomic_bool domReady{};
        std::atomic_bool viewRequested{};
        std::atomic_bool panelVisible{};
        std::atomic_bool worldPresentationActive{};
        std::atomic_bool pushScheduled{};
        std::atomic_bool discoveryStarted{};
        std::jthread discoveryThread;

        std::atomic_uint64_t ownerToken{};
        std::atomic_uint64_t frameCallbackToken{};
        std::atomic_bool overlayCapability{};
        std::atomic_bool diagnosticsEnabled{};
        std::atomic_bool overlayPublished{};
        RockProviderDebugOverlayTextV1 overlayText{};

        std::mutex frameMutex;
        FrameData latestFrame{};
        std::mutex settingsMutex;
        linear_lighting::Settings uiSettings{};
        std::atomic_uint64_t uiRevision{ 1 };

        std::atomic_uint64_t spatialSequence{ 1 };
        std::atomic_uint64_t pointerSequence{ 1 };
        std::atomic_uint64_t routedFramePlusOne{};
        std::atomic_bool pointerActive{};
        std::mutex pointerApiMutex;
        std::mutex clickMutex;
        pointer_click_gate::State clickState{};
        std::chrono::steady_clock::time_point nextModelPublish{};
        std::uint64_t lastPublishedUiRevision{};
        std::uint64_t lastPublishedDiagnosticsRevision{};

        void pushLatestSnapshot() noexcept;

        [[nodiscard]] std::optional<std::array<float, 4>>
        quaternionFromBasis(
            const Vec3& right,
            const Vec3& up,
            const Vec3& front) noexcept
        {
            const float m00 = right.x;
            const float m01 = up.x;
            const float m02 = front.x;
            const float m10 = right.y;
            const float m11 = up.y;
            const float m12 = front.y;
            const float m20 = right.z;
            const float m21 = up.z;
            const float m22 = front.z;
            std::array<float, 4> value{};
            const auto trace = m00 + m11 + m22;
            if (trace > 0.0f) {
                const auto scale = std::sqrt(trace + 1.0f) * 2.0f;
                if (!std::isfinite(scale) || scale < 0.0001f) {
                    return std::nullopt;
                }
                value = { (m21 - m12) / scale, (m02 - m20) / scale,
                    (m10 - m01) / scale, 0.25f * scale };
            } else if (m00 > m11 && m00 > m22) {
                const auto scale =
                    std::sqrt((std::max)(0.0f, 1.0f + m00 - m11 - m22)) *
                    2.0f;
                if (!std::isfinite(scale) || scale < 0.0001f) {
                    return std::nullopt;
                }
                value = { 0.25f * scale, (m01 + m10) / scale,
                    (m02 + m20) / scale, (m21 - m12) / scale };
            } else if (m11 > m22) {
                const auto scale =
                    std::sqrt((std::max)(0.0f, 1.0f + m11 - m00 - m22)) *
                    2.0f;
                if (!std::isfinite(scale) || scale < 0.0001f) {
                    return std::nullopt;
                }
                value = { (m01 + m10) / scale, 0.25f * scale,
                    (m12 + m21) / scale, (m02 - m20) / scale };
            } else {
                const auto scale =
                    std::sqrt((std::max)(0.0f, 1.0f + m22 - m00 - m11)) *
                    2.0f;
                if (!std::isfinite(scale) || scale < 0.0001f) {
                    return std::nullopt;
                }
                value = { (m02 + m20) / scale, (m12 + m21) / scale,
                    0.25f * scale, (m10 - m01) / scale };
            }
            float normSquared{};
            for (const auto component : value) {
                if (!std::isfinite(component)) {
                    return std::nullopt;
                }
                normSquared += component * component;
            }
            if (!std::isfinite(normSquared) || normSquared < 1.0e-8f) {
                return std::nullopt;
            }
            const auto inverseNorm = 1.0f / std::sqrt(normSquared);
            for (auto& component : value) {
                component *= inverseNorm;
            }
            return value;
        }

        [[nodiscard]] bool snapshotAllowsDisplay(
            const RockProviderFrameSnapshot& snapshot) noexcept
        {
            return snapshot.providerReady != 0 && snapshot.menuBlocking == 0 &&
                   snapshot.configBlocking == 0 &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::WorldAvailable) &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::SkeletonReady) &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::ProviderReady);
        }

        [[nodiscard]] std::optional<PanelPose> computePanelPose(
            const RockProviderFrameSnapshot& snapshot) noexcept
        {
            const auto& transform = snapshot.rightHandTransform;
            const Vec3 origin{ transform.translate[0], transform.translate[1],
                transform.translate[2] };
            const auto forward = normalize(
                { transform.rotate[0], transform.rotate[1],
                    transform.rotate[2] });
            const Vec3 rawLateral{ transform.rotate[3], transform.rotate[4],
                transform.rotate[5] };
            const Vec3 rawUp{ transform.rotate[6], transform.rotate[7],
                transform.rotate[8] };
            if (!finite(origin) || !forward) {
                return std::nullopt;
            }
            auto up = normalize(rawUp - *forward * dot(rawUp, *forward));
            if (!up) {
                const auto lateral = normalize(
                    rawLateral - *forward * dot(rawLateral, *forward));
                if (!lateral) {
                    return std::nullopt;
                }
                up = normalize(cross(*forward, *lateral));
            }
            if (!up) {
                return std::nullopt;
            }
            const auto handLateral = normalize(cross(*up, *forward));
            if (!handLateral) {
                return std::nullopt;
            }
            up = normalize(cross(*forward, *handLateral));
            if (!up) {
                return std::nullopt;
            }
            const auto orientation = quaternionFromBasis(
                *handLateral * -1.0f,
                *up,
                *forward * -1.0f);
            if (!orientation) {
                return std::nullopt;
            }
            PanelPose result{};
            result.position = origin + *forward * kPanelForward +
                              *handLateral * kPanelLateral + *up * kPanelUp;
            result.orientation = *orientation;
            return finite(result.position) ?
                std::optional<PanelPose>{ result } : std::nullopt;
        }

        [[nodiscard]] std::optional<PointerRay> computePointerRay(
            const RockProviderFrameSnapshot& snapshot) noexcept
        {
            const auto& transform = snapshot.leftHandTransform;
            const Vec3 origin{ transform.translate[0], transform.translate[1],
                transform.translate[2] };
            const auto direction = normalize(
                { transform.rotate[0], transform.rotate[1],
                    transform.rotate[2] });
            if (!finite(origin) || !direction ||
                !std::isfinite(spatialCapabilities.maxAbsoluteWorldPosition) ||
                std::fabs(origin.x) > spatialCapabilities.maxAbsoluteWorldPosition ||
                std::fabs(origin.y) > spatialCapabilities.maxAbsoluteWorldPosition ||
                std::fabs(origin.z) > spatialCapabilities.maxAbsoluteWorldPosition) {
                return std::nullopt;
            }
            return PointerRay{ origin, *direction };
        }

        void clearSuppressionLocked() noexcept
        {
            const auto token = ownerToken.load(std::memory_order_acquire);
            if (token && RockProviderApi::inst &&
                RockProviderApi::inst->clearHandInputSuppressionV1) {
                (void)RockProviderApi::inst->clearHandInputSuppressionV1(
                    token,
                    RockProviderHand::Left);
            }
        }

        void resetClickInput(bool forceClear = false) noexcept
        {
            routedFramePlusOne.store(0, std::memory_order_release);
            std::scoped_lock lock(clickMutex);
            if (pointer_click_gate::reset(clickState) || forceClear) {
                clearSuppressionLocked();
            }
        }

        [[nodiscard]] bool samplePrimaryLevel(
            const RockProviderFrameSnapshot& snapshot) noexcept
        {
            const auto encoded =
                routedFramePlusOne.load(std::memory_order_acquire);
            const auto routedFrame = encoded ? encoded - 1 : 0;
            const auto routed = encoded != 0 &&
                snapshot.frameIndex >= routedFrame &&
                snapshot.frameIndex - routedFrame <= kRouteFreshnessFrames;
            RockProviderRawWandButtonStateV1 trigger{};
            RockProviderRawWandButtonStateV1 xButton{};
            const auto rawAvailable = RockProviderApi::inst &&
                RockProviderApi::inst->getRawWandButtonStateV1 &&
                RockProviderApi::inst->getRawWandButtonStateV1(
                    RockProviderHand::Left,
                    kLeftTriggerButtonId,
                    &trigger) &&
                RockProviderApi::inst->getRawWandButtonStateV1(
                    RockProviderHand::Left,
                    kLeftXButtonId,
                    &xButton) &&
                trigger.available != 0 && xButton.available != 0;
            const auto down = rawAvailable &&
                (trigger.held != 0 || xButton.held != 0);

            auto leaseAccepted = false;
            if (routed && rawAvailable) {
                const auto token = ownerToken.load(std::memory_order_acquire);
                if (token && RockProviderApi::inst &&
                    RockProviderApi::inst->setHandInputSuppressionV1) {
                    RockProviderHandInputSuppressionRequestV1 request{};
                    request.hand = RockProviderHand::Left;
                    request.flags = static_cast<std::uint32_t>(
                                        RockProviderHandInputSuppressionFlagV1::
                                            SuppressConfigModeChord) |
                                    static_cast<std::uint32_t>(
                                        RockProviderHandInputSuppressionFlagV1::
                                            SuppressOpenVrGameInput);
                    request.leaseFrames = kSuppressionLeaseFrames;
                    request.worldGeneration = snapshot.worldGeneration;
                    request.skeletonGeneration = snapshot.skeletonGeneration;
                    request.providerGeneration = snapshot.providerGeneration;
                    leaseAccepted =
                        RockProviderApi::inst->setHandInputSuppressionV1(
                            token,
                            &request) == RockProviderResultV1::Ok;
                }
            }

            std::scoped_lock lock(clickMutex);
            const auto result = pointer_click_gate::advance(
                clickState,
                snapshot.frameIndex,
                routed,
                rawAvailable,
                down,
                leaseAccepted);
            if (result.clearLease) {
                clearSuppressionLocked();
            }
            return result.forwardPrimaryDown;
        }

        [[nodiscard]] bool accepted(
            PRISMA_UI_VR_API::SpatialResult result) noexcept
        {
            return result == PRISMA_UI_VR_API::SpatialResult::Ok ||
                   result ==
                       PRISMA_UI_VR_API::SpatialResult::PendingUpdateReplaced;
        }

        [[nodiscard]] std::optional<std::uint64_t> nextSequence(
            std::atomic_uint64_t& source) noexcept
        {
            auto value = source.load(std::memory_order_relaxed);
            while (value != (std::numeric_limits<std::uint64_t>::max)()) {
                if (source.compare_exchange_weak(
                        value,
                        value + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    return value;
                }
            }
            return std::nullopt;
        }

        void cancelPointer(bool force = false) noexcept
        {
            resetClickInput(force);
            const auto wasActive =
                pointerActive.exchange(false, std::memory_order_acq_rel);
            if ((!wasActive && !force) || !prismaVr || !view) {
                return;
            }
            std::scoped_lock lock(pointerApiMutex);
            (void)prismaVr->CancelSpatialPointer(view);
        }

        void submitPointer(
            const PointerRay& ray,
            bool primaryDown,
            std::uint64_t frameIndex) noexcept
        {
            const auto sequence = nextSequence(pointerSequence);
            if (!sequence || !prismaVr || !view) {
                cancelPointer();
                return;
            }
            PRISMA_UI_VR_API::SpatialPointerUpdateV1 update{};
            update.structSize = sizeof(update);
            update.coordinateSpace =
                PRISMA_UI_VR_API::SpatialCoordinateSpace::GameWorld;
            update.flags =
                PRISMA_UI_VR_API::SpatialPointerUpdate_Active;
            update.buttonLevels = primaryDown ?
                PRISMA_UI_VR_API::SpatialPointerButton_Primary : 0u;
            update.sequence = *sequence;
            update.rayOrigin[0] = ray.origin.x;
            update.rayOrigin[1] = ray.origin.y;
            update.rayOrigin[2] = ray.origin.z;
            update.maxDistance = kPointerMaxDistance;
            update.rayDirection[0] = ray.direction.x;
            update.rayDirection[1] = ray.direction.y;
            update.rayDirection[2] = ray.direction.z;
            update.pointerSourceId =
                PRISMA_UI_VR_API::SpatialPointerSource_PhysicalLeftController;

            std::scoped_lock lock(pointerApiMutex);
            PRISMA_UI_VR_API::SpatialPointerStateV1 state{};
            state.structSize = sizeof(state);
            if (prismaVr->GetSpatialPointerState(view, &state) ==
                PRISMA_UI_VR_API::SpatialResult::Ok) {
                constexpr std::uint32_t routeFlags =
                    PRISMA_UI_VR_API::SpatialPointerState_Active |
                    PRISMA_UI_VR_API::SpatialPointerState_Applied |
                    PRISMA_UI_VR_API::SpatialPointerState_BackendReady |
                    PRISMA_UI_VR_API::SpatialPointerState_Routed;
                const auto routed = state.pointerSourceId ==
                        PRISMA_UI_VR_API::
                            SpatialPointerSource_PhysicalLeftController &&
                    (state.stateFlags & routeFlags) == routeFlags &&
                    (state.stateFlags &
                        (PRISMA_UI_VR_API::SpatialPointerState_Hit |
                            PRISMA_UI_VR_API::
                                SpatialPointerState_Captured)) != 0;
                routedFramePlusOne.store(
                    routed && frameIndex !=
                            (std::numeric_limits<std::uint64_t>::max)() ?
                        frameIndex + 1 : 0,
                    std::memory_order_release);
            } else {
                routedFramePlusOne.store(0, std::memory_order_release);
            }
            const auto result =
                prismaVr->SubmitSpatialPointerUpdate(view, &update);
            pointerActive.store(accepted(result), std::memory_order_release);
        }

        [[nodiscard]] nlohmann::json settingsJson(
            const linear_lighting::Settings& settings)
        {
            return {
                { "enabled", settings.enabled },
                { "lightGamma", settings.lightGamma },
                { "colorGamma", settings.colorGamma },
                { "emitColorGamma", settings.emitColorGamma },
                { "glowmapGamma", settings.glowmapGamma },
                { "ambientGamma", settings.ambientGamma },
                { "fogGamma", settings.fogGamma },
                { "fogAlphaGamma", settings.fogAlphaGamma },
                { "effectGamma", settings.effectGamma },
                { "effectAlphaGamma", settings.effectAlphaGamma },
                { "skyGamma", settings.skyGamma },
                { "waterGamma", settings.waterGamma },
                { "volumetricLightingGamma",
                    settings.volumetricLightingGamma },
                { "vanillaDiffuseColorMultiplier",
                    settings.vanillaDiffuseColorMultiplier },
                { "directionalLightMultiplier",
                    settings.directionalLightMultiplier },
                { "pointLightMultiplier", settings.pointLightMultiplier },
                { "ambientMultiplier", settings.ambientMultiplier },
                { "emitColorMultiplier", settings.emitColorMultiplier },
                { "glowmapMultiplier", settings.glowmapMultiplier },
                { "effectLightingMultiplier",
                    settings.effectLightingMultiplier },
            };
        }

        [[nodiscard]] std::uint64_t diagnosticsRevision() noexcept
        {
            const auto runtime = linear_lighting::Runtime::get().snapshot();
            const auto geometry = render::geometryHookSnapshot();
            const auto d3d = render::d3d11HookSnapshot();
            return runtime.replacementBinds ^ (runtime.geometryUpdates << 1) ^
                (geometry.calls << 2) ^ (d3d.pixelShaderBindCalls << 3);
        }

        [[nodiscard]] std::string buildModelJson()
        {
            linear_lighting::Settings settings{};
            {
                std::scoped_lock lock(settingsMutex);
                settings = uiSettings;
            }
            const auto runtime = linear_lighting::Runtime::get().snapshot();
            const auto geometry = render::geometryHookSnapshot();
            const auto d3d = render::d3d11HookSnapshot();
            nlohmann::json model{
                { "revision", uiRevision.load(std::memory_order_acquire) },
                { "settings", settingsJson(settings) },
                { "coverage",
                    {
                        { "label", "Projected opaque / five MRT / base + vertex color" },
                        { "verifiedShaderContracts",
                            runtime.verifiedShaderContracts },
                        { "fullFeaturePort", false },
                    } },
                { "runtime",
                    {
                        { "enabled", runtime.enabled },
                        { "gpuReady", runtime.gpuResourcesReady },
                        { "geometryReady", runtime.geometryProviderReady },
                        { "matchingShaders", runtime.matchingShadersCreated },
                        { "trackedShaders", runtime.trackedOriginalShaders },
                        { "replacementBinds", runtime.replacementBinds },
                        { "geometryUpdates", runtime.geometryUpdates },
                        { "geometryRejects",
                            runtime.rejectedGeometryUpdates },
                    } },
                { "hooks",
                    {
                        { "d3dImport", d3d.deviceCreationImportInstalled },
                        { "deviceCaptured", d3d.deviceCaptured },
                        { "deviceHooks", d3d.deviceHooksInstalled },
                        { "pixelShaderCreates",
                            d3d.pixelShaderCreationCalls },
                        { "pixelShaderBinds", d3d.pixelShaderBindCalls },
                        { "geometryInstalled", geometry.installed },
                        { "geometryCalls", geometry.calls },
                        { "geometryAccepted", geometry.acceptedUpdates },
                        { "geometryRejected", geometry.rejectedWalks },
                        { "deepestGeometryStage",
                            static_cast<std::uint32_t>(geometry.deepestStage) },
                    } },
                { "wrist",
                    {
                        { "prisma", prisma != nullptr },
                        { "prismaVr", prismaVr != nullptr },
                        { "domReady", domReady.load(std::memory_order_acquire) },
                        { "rock", frameCallbackToken.load(
                                      std::memory_order_acquire) != 0 },
                        { "panelVisible",
                            panelVisible.load(std::memory_order_acquire) },
                        { "overlayAvailable",
                            overlayCapability.load(std::memory_order_acquire) },
                        { "overlayEnabled",
                            diagnosticsEnabled.load(std::memory_order_acquire) },
                    } },
            };
            return model.dump();
        }

        void publishModelIfNeeded() noexcept
        {
            if (!prisma || !view || !domReady.load(std::memory_order_acquire)) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto revision = uiRevision.load(std::memory_order_acquire);
            const auto diagnostic = diagnosticsRevision();
            if (now < nextModelPublish && revision == lastPublishedUiRevision &&
                diagnostic == lastPublishedDiagnosticsRevision) {
                return;
            }
            nextModelPublish = now + kModelPublishInterval;
            const auto payload = buildModelJson();
            prisma->InteropCall(
                view,
                "communityShadersUpdate",
                payload.c_str());
            lastPublishedUiRevision = revision;
            lastPublishedDiagnosticsRevision = diagnostic;
        }

        void hidePanel() noexcept
        {
            cancelPointer();
            if (prisma && view &&
                panelVisible.exchange(false, std::memory_order_acq_rel)) {
                prisma->Hide(view);
            }
            worldPresentationActive.store(false, std::memory_order_release);
        }

        [[nodiscard]] bool poseWithinCapabilities(
            const PanelPose& pose) noexcept
        {
            if (!finite(pose.position) ||
                std::fabs(pose.position.x) >
                    spatialCapabilities.maxAbsoluteWorldPosition ||
                std::fabs(pose.position.y) >
                    spatialCapabilities.maxAbsoluteWorldPosition ||
                std::fabs(pose.position.z) >
                    spatialCapabilities.maxAbsoluteWorldPosition) {
                return false;
            }
            float normSquared{};
            for (const auto component : pose.orientation) {
                if (!std::isfinite(component)) {
                    return false;
                }
                normSquared += component * component;
            }
            return normSquared >= spatialCapabilities.minQuaternionNormSquared;
        }

        void schedulePush() noexcept
        {
            auto expected = false;
            if (!pushScheduled.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel)) {
                return;
            }
            const auto* tasks = F4SE::GetTaskInterface();
            if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion) {
                pushScheduled.store(false, std::memory_order_release);
                return;
            }
            // The canonical F4SEVR task interface owns and deletes this bounded
            // delegate after its game-thread Run call. Coalescing guarantees at
            // most one queued wrist update at a time.
            tasks->AddTask([]() { pushLatestSnapshot(); });
        }

        void pushLatestSnapshot() noexcept
        {
            pushScheduled.store(false, std::memory_order_release);
            if (!prisma || !prismaVr || !view ||
                !domReady.load(std::memory_order_acquire)) {
                return;
            }
            FrameData frame{};
            {
                std::scoped_lock lock(frameMutex);
                frame = latestFrame;
            }
            if (!frame.ready || !frame.panelPose ||
                !poseWithinCapabilities(*frame.panelPose)) {
                hidePanel();
                return;
            }

            const auto sequence = nextSequence(spatialSequence);
            if (!sequence) {
                hidePanel();
                return;
            }
            PRISMA_UI_VR_API::SpatialUpdateV1 update{};
            update.structSize = sizeof(update);
            update.coordinateSpace =
                PRISMA_UI_VR_API::SpatialCoordinateSpace::GameWorld;
            update.presentationMode =
                PRISMA_UI_VR_API::SpatialPresentationMode::WorldQuad;
            update.flags =
                PRISMA_UI_VR_API::SpatialUpdate_SceneDepthOcclusion;
            update.sequence = *sequence;
            update.dimensions.pixelWidth = kPanelWidthPixels;
            update.dimensions.pixelHeight = kPanelHeightPixels;
            update.dimensions.physicalWidth = kPanelPhysicalWidth;
            update.dimensions.physicalHeight = kPanelPhysicalHeight;
            update.pose.position[0] = frame.panelPose->position.x;
            update.pose.position[1] = frame.panelPose->position.y;
            update.pose.position[2] = frame.panelPose->position.z;
            std::copy(
                frame.panelPose->orientation.begin(),
                frame.panelPose->orientation.end(),
                update.pose.orientation);
            if (!accepted(prismaVr->SubmitSpatialUpdate(view, &update))) {
                hidePanel();
                return;
            }
            worldPresentationActive.store(true, std::memory_order_release);
            if (!panelVisible.exchange(true, std::memory_order_acq_rel)) {
                prisma->Show(view);
            }
            if (frame.pointerRay) {
                submitPointer(
                    *frame.pointerRay,
                    frame.pointerPrimaryDown,
                    frame.frameIndex);
            } else {
                cancelPointer();
            }
            publishModelIfNeeded();
        }

        [[nodiscard]] bool setFloatSetting(
            linear_lighting::Settings& settings,
            std::string_view key,
            float value) noexcept
        {
#define CS_FLOAT_SETTING(NAME) \
    if (key == #NAME) {         \
        settings.NAME = value;  \
        return true;            \
    }
            CS_FLOAT_SETTING(lightGamma)
            CS_FLOAT_SETTING(colorGamma)
            CS_FLOAT_SETTING(emitColorGamma)
            CS_FLOAT_SETTING(glowmapGamma)
            CS_FLOAT_SETTING(ambientGamma)
            CS_FLOAT_SETTING(fogGamma)
            CS_FLOAT_SETTING(fogAlphaGamma)
            CS_FLOAT_SETTING(effectGamma)
            CS_FLOAT_SETTING(effectAlphaGamma)
            CS_FLOAT_SETTING(skyGamma)
            CS_FLOAT_SETTING(waterGamma)
            CS_FLOAT_SETTING(volumetricLightingGamma)
            CS_FLOAT_SETTING(vanillaDiffuseColorMultiplier)
            CS_FLOAT_SETTING(directionalLightMultiplier)
            CS_FLOAT_SETTING(pointLightMultiplier)
            CS_FLOAT_SETTING(ambientMultiplier)
            CS_FLOAT_SETTING(emitColorMultiplier)
            CS_FLOAT_SETTING(glowmapMultiplier)
            CS_FLOAT_SETTING(effectLightingMultiplier)
#undef CS_FLOAT_SETTING
            return false;
        }

        void handleUiAction(const char* payload) noexcept
        {
            try {
                if (!payload || std::strlen(payload) > 4096) {
                    return;
                }
                const auto action = nlohmann::json::parse(
                    payload,
                    nullptr,
                    false,
                    true);
                if (action.is_discarded() || !action.is_object()) {
                    return;
                }
                const auto type = action.value("type", std::string{});
                if (type == "diagnostics") {
                    diagnosticsEnabled.store(
                        action.value("enabled", false),
                        std::memory_order_release);
                    uiRevision.fetch_add(1, std::memory_order_release);
                    schedulePush();
                    return;
                }

                linear_lighting::Settings next{};
                {
                    std::scoped_lock lock(settingsMutex);
                    next = uiSettings;
                }
                auto changed = false;
                if (type == "reset") {
                    next = {};
                    changed = true;
                } else if (type == "enabled" && action.contains("value") &&
                           action["value"].is_boolean()) {
                    next.enabled = action["value"].get<bool>();
                    changed = true;
                } else if (type == "set" && action.contains("key") &&
                           action["key"].is_string() &&
                           action.contains("value") &&
                           action["value"].is_number()) {
                    const auto value = action["value"].get<float>();
                    changed = std::isfinite(value) && setFloatSetting(
                        next,
                        action["key"].get<std::string>(),
                        value);
                }
                if (!changed) {
                    return;
                }
                next = linear_lighting::sanitize(next);
                {
                    std::scoped_lock lock(settingsMutex);
                    uiSettings = next;
                }
                linear_lighting::Runtime::get().queueSettings(next);
                const auto saved = linear_lighting::saveSettings(next);
                uiRevision.fetch_add(1, std::memory_order_release);
                logging::info(
                    "Linear Lighting wrist action '{}' accepted; settings save={}.",
                    type,
                    saved);
                schedulePush();
            } catch (const std::exception& error) {
                logging::warn(
                    "Linear Lighting wrist action rejected: {}",
                    error.what());
            } catch (...) {
                logging::warn(
                    "Linear Lighting wrist action rejected by an unknown exception.");
            }
        }

        void publishDeveloperOverlay(
            const RockProviderFrameSnapshot& snapshot) noexcept
        {
            const auto token = ownerToken.load(std::memory_order_acquire);
            if (!token || !overlayCapability.load(std::memory_order_acquire) ||
                !RockProviderApi::inst) {
                return;
            }
            if (!diagnosticsEnabled.load(std::memory_order_acquire)) {
                if (overlayPublished.exchange(false, std::memory_order_acq_rel) &&
                    RockProviderApi::inst->clearDebugOverlayV1) {
                    (void)RockProviderApi::inst->clearDebugOverlayV1(token);
                }
                return;
            }
            if (!RockProviderApi::inst->publishDebugOverlayV1) {
                return;
            }

            // Formatting is intentionally bounded to one in 15 provider
            // frames; the immutable POD publication is renewed every frame.
            if ((snapshot.frameIndex % 15) == 0 || !overlayPublished.load()) {
                const auto runtime = linear_lighting::Runtime::get().snapshot();
                const auto geometry = render::geometryHookSnapshot();
                const auto d3d = render::d3d11HookSnapshot();
                overlayText = {};
                overlayText.x = 18.0f;
                overlayText.y = 280.0f;
                overlayText.textSize = 1.65f;
                overlayText.color[0] = 0.96f;
                overlayText.color[1] = 0.72f;
                overlayText.color[2] = 0.28f;
                overlayText.color[3] = 0.94f;
                std::snprintf(
                    overlayText.text,
                    sizeof(overlayText.text),
                    "COMMUNITY SHADERS / LINEAR LIGHTING\n"
                    "enabled %u | gpu %u | geometry %u | candidates %u\n"
                    "replacement binds %llu | geometry updates %llu\n"
                    "D3D PS binds %llu | hook calls %llu | stage %u",
                    runtime.enabled,
                    runtime.gpuResourcesReady,
                    runtime.geometryProviderReady,
                    runtime.matchingShadersCreated,
                    static_cast<unsigned long long>(runtime.replacementBinds),
                    static_cast<unsigned long long>(runtime.geometryUpdates),
                    static_cast<unsigned long long>(d3d.pixelShaderBindCalls),
                    static_cast<unsigned long long>(geometry.calls),
                    static_cast<std::uint32_t>(geometry.deepestStage));
            }
            RockProviderDebugOverlayPublicationV1 publication{};
            publication.textCount = 1;
            publication.textEntries = &overlayText;
            publication.worldGeneration = snapshot.worldGeneration;
            publication.skeletonGeneration = snapshot.skeletonGeneration;
            publication.providerGeneration = snapshot.providerGeneration;
            publication.leaseFrames = 3;
            const auto result = RockProviderApi::inst->publishDebugOverlayV1(
                token,
                &publication);
            overlayPublished.store(
                result == RockProviderResultV1::Ok,
                std::memory_order_release);
        }

        void ROCK_PROVIDER_CALL onRockFrame(
            const RockProviderFrameSnapshot* snapshot,
            void*) noexcept
        {
            if (!snapshot || snapshot->size < sizeof(*snapshot)) {
                return;
            }
            publishDeveloperOverlay(*snapshot);
            FrameData frame{};
            frame.frameIndex = snapshot->frameIndex;
            frame.ready = snapshotAllowsDisplay(*snapshot);
            if (frame.ready) {
                frame.panelPose = computePanelPose(*snapshot);
                frame.pointerRay = computePointerRay(*snapshot);
                if (frame.panelPose && frame.pointerRay) {
                    frame.pointerPrimaryDown = samplePrimaryLevel(*snapshot);
                } else {
                    resetClickInput();
                }
            } else {
                resetClickInput();
            }
            {
                std::scoped_lock lock(frameMutex);
                latestFrame = frame;
            }
            schedulePush();
        }

        [[nodiscard]] bool registerWithRock(bool logFailure) noexcept
        {
            if (frameCallbackToken.load(std::memory_order_acquire)) {
                return true;
            }
            const auto error = RockProviderApi::initialize(
                rock::provider::ROCK_PROVIDER_API_VERSION,
                rock::provider::
                    ROCK_PROVIDER_API_V1_OWNER_FRAME_CALLBACKS_TABLE_BYTES);
            if (error != 0 || !RockProviderApi::inst) {
                if (logFailure) {
                    logging::warn(
                        "Community Shaders wrist is waiting for ROCK provider (error {}).",
                        error);
                }
                return false;
            }
            RockProviderLimitsV1 limits{};
            if (!RockProviderApi::inst->getProviderLimitsV1 ||
                !RockProviderApi::inst->getProviderLimitsV1(&limits) ||
                !rock::provider::supportsHandInputSuppressionV1(limits) ||
                !rock::provider::supportsRawWandButtonStateV1(limits) ||
                !RockProviderApi::inst->registerConsumerV1 ||
                !RockProviderApi::inst->registerFrameCallbackForOwnerV1 ||
                !RockProviderApi::inst->unregisterConsumerV1 ||
                !RockProviderApi::inst->setHandInputSuppressionV1 ||
                !RockProviderApi::inst->clearHandInputSuppressionV1 ||
                !RockProviderApi::inst->getRawWandButtonStateV1) {
                if (logFailure) {
                    logging::warn(
                        "Community Shaders wrist requires ROCK frame, raw-wand, and suppression contracts.");
                }
                return false;
            }
            const auto overlaySupported =
                rock::provider::supportsDebugOverlayPublicationV1(limits) &&
                RockProviderApi::inst->publishDebugOverlayV1 &&
                RockProviderApi::inst->clearDebugOverlayV1;
            RockProviderConsumerRegistrationV1 registration{};
            std::snprintf(
                registration.modName,
                sizeof(registration.modName),
                "FO4VR Community Shaders");
            registration.requestedCapabilities =
                static_cast<std::uint32_t>(
                    RockProviderConsumerCapabilityV1::FrameSnapshots) |
                static_cast<std::uint32_t>(
                    RockProviderConsumerCapabilityV1::HandInputSuppression) |
                (overlaySupported ?
                        static_cast<std::uint32_t>(
                            RockProviderConsumerCapabilityV1::
                                DebugOverlayPublication) :
                        0u);
            RockProviderConsumerHandleV1 handle{};
            const auto result = RockProviderApi::inst->registerConsumerV1(
                &registration,
                &handle);
            const auto requiredGranted = result == RockProviderResultV1::Ok &&
                handle.ownerToken != 0 &&
                rock::provider::hasConsumerCapabilityV1(
                    handle.grantedCapabilities,
                    RockProviderConsumerCapabilityV1::FrameSnapshots) &&
                rock::provider::hasConsumerCapabilityV1(
                    handle.grantedCapabilities,
                    RockProviderConsumerCapabilityV1::HandInputSuppression);
            if (!requiredGranted) {
                if (handle.ownerToken) {
                    (void)RockProviderApi::inst->unregisterConsumerV1(
                        handle.ownerToken);
                }
                return false;
            }
            ownerToken.store(handle.ownerToken, std::memory_order_release);
            overlayCapability.store(
                overlaySupported && rock::provider::hasConsumerCapabilityV1(
                    handle.grantedCapabilities,
                    RockProviderConsumerCapabilityV1::
                        DebugOverlayPublication),
                std::memory_order_release);
            std::uint64_t callback{};
            const auto callbackResult =
                RockProviderApi::inst->registerFrameCallbackForOwnerV1(
                    handle.ownerToken,
                    &onRockFrame,
                    nullptr,
                    &callback);
            if (callbackResult != RockProviderResultV1::Ok || !callback) {
                (void)RockProviderApi::inst->unregisterConsumerV1(
                    handle.ownerToken);
                ownerToken.store(0, std::memory_order_release);
                overlayCapability.store(false, std::memory_order_release);
                return false;
            }
            frameCallbackToken.store(callback, std::memory_order_release);
            logging::info(
                "Community Shaders wrist connected to ROCK; debug overlay capability={}.",
                overlayCapability.load(std::memory_order_acquire));
            return true;
        }

        void startRockDiscovery() noexcept
        {
            auto expected = false;
            if (!discoveryStarted.compare_exchange_strong(expected, true)) {
                return;
            }
            discoveryThread = std::jthread([](std::stop_token stop) {
                for (std::uint32_t attempt{}; !stop.stop_requested(); ++attempt) {
                    if (registerWithRock(attempt == 0 || (attempt % 10) == 9)) {
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            });
        }

        void onDomReady(PrismaView readyView) noexcept
        {
            if (!prisma || readyView != view) {
                return;
            }
            prisma->Hide(view);
            domReady.store(true, std::memory_order_release);
            logging::info(
                "Community Shaders wrist DOM ready (view {}).",
                view);
            schedulePush();
        }

        [[nodiscard]] bool acquireSpatialCapabilities() noexcept
        {
            if (!prismaVr) {
                return false;
            }
            spatialCapabilities = {};
            spatialCapabilities.structSize = sizeof(spatialCapabilities);
            if (prismaVr->GetSpatialCapabilities(&spatialCapabilities) !=
                PRISMA_UI_VR_API::SpatialResult::Ok) {
                return false;
            }
            const auto worldMask = 1ull << static_cast<std::uint32_t>(
                PRISMA_UI_VR_API::SpatialCoordinateSpace::GameWorld);
            const auto quadMask = 1ull << static_cast<std::uint32_t>(
                PRISMA_UI_VR_API::SpatialPresentationMode::WorldQuad);
            return spatialCapabilities.structSize >=
                       sizeof(spatialCapabilities) &&
                spatialCapabilities.apiFlavor == kF4VrApiFlavor &&
                (spatialCapabilities.featureBits & kRequiredSpatialFeatures) ==
                    kRequiredSpatialFeatures &&
                (spatialCapabilities.coordinateSpaceMask & worldMask) != 0 &&
                (spatialCapabilities.presentationModeMask & quadMask) != 0 &&
                (spatialCapabilities.supportedUpdateFlags &
                    PRISMA_UI_VR_API::
                        SpatialUpdate_SceneDepthOcclusion) != 0 &&
                spatialCapabilities.maxPixelWidth >= kPanelWidthPixels &&
                spatialCapabilities.maxPixelHeight >= kPanelHeightPixels &&
                spatialCapabilities.maxSpatialViews > 0 &&
                spatialCapabilities.maxAggregateSpatialPixels >=
                    static_cast<std::uint64_t>(kPanelWidthPixels) *
                        kPanelHeightPixels &&
                std::isfinite(spatialCapabilities.maxAbsoluteWorldPosition) &&
                spatialCapabilities.maxAbsoluteWorldPosition > 0.0f &&
                std::isfinite(spatialCapabilities.maxPhysicalDimension) &&
                spatialCapabilities.maxPhysicalDimension >= kPanelPhysicalWidth &&
                std::isfinite(spatialCapabilities.minQuaternionNormSquared) &&
                spatialCapabilities.minQuaternionNormSquared > 0.0f;
        }

        void ensureView() noexcept
        {
            if (!prisma || !prismaVr || view) {
                return;
            }
            auto expected = false;
            if (!viewRequested.compare_exchange_strong(expected, true)) {
                return;
            }
            PRISMA_UI_VR_API::ViewCreateOptionsV1 options{};
            options.structSize = sizeof(options);
            options.networkAccessPolicy =
                PRISMA_UI_VR_API::NetworkAccessPolicy::LocalOnly;
            view = prismaVr->CreateViewWithOptions(
                "FO4VR-Community-Shaders/index.html",
                &onDomReady,
                &options);
            if (!view) {
                viewRequested.store(false, std::memory_order_release);
                logging::error(
                    "Community Shaders failed to create its optional Prisma wrist view.");
                return;
            }
            prisma->Hide(view);
            prisma->BindUIEvent(
                view,
                "communityShadersAction",
                &handleUiAction);
            PRISMA_UI_VR_API::NetworkAccessPolicy policy{};
            if (!prismaVr->GetNetworkAccessPolicy(view, &policy) ||
                policy != PRISMA_UI_VR_API::NetworkAccessPolicy::LocalOnly) {
                prisma->Destroy(view);
                view = 0;
                viewRequested.store(false, std::memory_order_release);
                logging::error(
                    "Community Shaders wrist rejected a non-LocalOnly Prisma view.");
                return;
            }
            prisma->RegisterConsoleCallback(
                view,
                [](PrismaView,
                    PRISMA_UI_API::ConsoleMessageLevel level,
                    const char* message) {
                    if (level == PRISMA_UI_API::ConsoleMessageLevel::Error) {
                        logging::error(
                            "Community Shaders wrist JS: {}",
                            message ? message : "");
                    }
                });
            prisma->SetOrder(view, 84);
        }
    }

    void setInitialSettings(
        const linear_lighting::Settings& settings) noexcept
    {
        std::scoped_lock lock(settingsMutex);
        uiSettings = linear_lighting::sanitize(settings);
        uiRevision.fetch_add(1, std::memory_order_release);
    }

    void onGameDataReady() noexcept
    {
        if (initialized.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        prisma =
            PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
        prismaVr = PRISMA_UI_VR_API::
            RequestPluginVRAPI<PRISMA_UI_VR_API::IVPrismaUIVR1>();
        if (!prisma || !prismaVr || !acquireSpatialCapabilities()) {
            prisma = nullptr;
            prismaVr = nullptr;
            logging::warn(
                "Community Shaders wrist UI is unavailable; core rendering and INI settings remain active.");
            return;
        }
        ensureView();
        startRockDiscovery();
        logging::info(
            "Community Shaders acquired optional Prisma FO4VR WorldQuad, scene-depth, and central-pointer contracts.");
    }

    void onGameSessionReady() noexcept
    {
        if (prisma && prismaVr) {
            ensureView();
        }
    }

    void shutdown() noexcept
    {
        if (discoveryThread.joinable()) {
            discoveryThread.request_stop();
            discoveryThread.join();
        }
        const auto token = ownerToken.load(std::memory_order_acquire);
        if (RockProviderApi::inst && token) {
            const auto callback =
                frameCallbackToken.exchange(0, std::memory_order_acq_rel);
            if (callback &&
                RockProviderApi::inst->unregisterFrameCallbackForOwnerV1) {
                (void)RockProviderApi::inst->unregisterFrameCallbackForOwnerV1(
                    token,
                    callback);
            }
            resetClickInput(true);
            if (RockProviderApi::inst->clearDebugOverlayV1) {
                (void)RockProviderApi::inst->clearDebugOverlayV1(token);
            }
            if (RockProviderApi::inst->unregisterConsumerV1) {
                (void)RockProviderApi::inst->unregisterConsumerV1(token);
            }
        }
        ownerToken.store(0, std::memory_order_release);
        cancelPointer(true);
        if (prisma && view) {
            prisma->Destroy(view);
        }
        view = 0;
        prisma = nullptr;
        prismaVr = nullptr;
    }
}
