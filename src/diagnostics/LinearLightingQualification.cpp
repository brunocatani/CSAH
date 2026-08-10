#include "diagnostics/LinearLightingQualification.h"

#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "diagnostics/LinearLightingQualificationModel.h"
#include "render/BSLightingGeometryHook.h"
#include "render/D3D11Hooks.h"
#include "support/Logger.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace community_shaders::diagnostics
{
    namespace
    {
        using qualification_model::Evaluation;
        using qualification_model::FailureReason;
        using qualification_model::Sample;
        using qualification_model::Status;

        constexpr auto kQualificationPollInterval =
            std::chrono::milliseconds(100);
        constexpr std::uint64_t kQualificationTimeoutMilliseconds = 20'000;
        constexpr const wchar_t* kQualificationReportFileName =
            L"FO4VRCommunityShaders.LinearLightingQualification.json";

        std::atomic<LinearLightingQualificationState> publicState{
            LinearLightingQualificationState::waitingForWorld };
        std::atomic_uint64_t publicGeneration{};
        std::atomic_uint64_t publicElapsedMilliseconds{};
        std::atomic_uint64_t publicReasonMask{};
        std::atomic_uint32_t publicFullyVerifiedContracts{};
        std::atomic_uint32_t publicExpectedContracts{
            static_cast<std::uint32_t>(
                linear_lighting::Runtime::kShaderContractCount) };
        std::atomic_bool publicWorldLifecycleReached{};
        std::atomic_bool publicRenderThreadActivated{};

        struct Session
        {
            std::uint64_t generation{};
            std::uint64_t d3dSessionId{};
            std::uint64_t startedTickMilliseconds{};
            std::uint64_t geometryCallsBaseline{};
            std::uint64_t geometryAcceptedBaseline{};
            std::uint64_t geometrySourceRejectedBaseline{};
            std::uint64_t geometryUpdatesBaseline{};
            std::uint64_t geometryUpdateRejectsBaseline{};
            std::string trigger;
        };

        struct Capture
        {
            linear_lighting::RuntimeSnapshot runtime;
            render::GeometryHookSnapshot geometry;
            render::HookSnapshot hooks;
            render::QualificationSnapshot d3d;
            Sample sample;
            std::uint64_t elapsedMilliseconds{};
        };

        [[nodiscard]] std::uint32_t contractCount(
            const linear_lighting::ContractMask& mask) noexcept
        {
            std::uint32_t result{};
            for (const auto word : mask) {
                result += static_cast<std::uint32_t>(std::popcount(word));
            }
            return result;
        }

        void publishPublicStatus(
            LinearLightingQualificationState state,
            const Session& session,
            const Capture& capture,
            const Evaluation& evaluation,
            bool worldLifecycleReached) noexcept
        {
            publicGeneration.store(
                session.generation,
                std::memory_order_relaxed);
            publicElapsedMilliseconds.store(
                capture.elapsedMilliseconds,
                std::memory_order_relaxed);
            publicReasonMask.store(
                evaluation.reasonMask,
                std::memory_order_relaxed);
            publicFullyVerifiedContracts.store(
                contractCount(evaluation.fullyVerifiedContractMask),
                std::memory_order_relaxed);
            publicExpectedContracts.store(
                capture.sample.expectedShaderContracts,
                std::memory_order_relaxed);
            publicWorldLifecycleReached.store(
                worldLifecycleReached,
                std::memory_order_relaxed);
            publicRenderThreadActivated.store(
                capture.sample.sessionActivated,
                std::memory_order_relaxed);
            publicState.store(state, std::memory_order_release);
        }

        void publishArmedSession(const Session& session) noexcept
        {
            publicGeneration.store(
                session.generation,
                std::memory_order_relaxed);
            publicElapsedMilliseconds.store(0, std::memory_order_relaxed);
            publicReasonMask.store(0, std::memory_order_relaxed);
            publicFullyVerifiedContracts.store(0, std::memory_order_relaxed);
            publicExpectedContracts.store(
                static_cast<std::uint32_t>(
                    linear_lighting::Runtime::kShaderContractCount),
                std::memory_order_relaxed);
            publicWorldLifecycleReached.store(true, std::memory_order_relaxed);
            publicRenderThreadActivated.store(false, std::memory_order_relaxed);
            publicState.store(
                LinearLightingQualificationState::running,
                std::memory_order_release);
        }

        [[nodiscard]] std::uint64_t delta(
            std::uint64_t value,
            std::uint64_t baseline) noexcept
        {
            return value >= baseline ? value - baseline : 0;
        }

        [[nodiscard]] const char* statusName(Status status) noexcept
        {
            switch (status) {
            case Status::passed:
                return "passed";
            case Status::failed:
                return "failed";
            default:
                return "running";
            }
        }

        void appendReason(
            nlohmann::json& reasons,
            std::uint64_t mask,
            FailureReason reason,
            const char* name)
        {
            if ((mask & static_cast<std::uint64_t>(reason)) != 0) {
                reasons.push_back(name);
            }
        }

        [[nodiscard]] nlohmann::json failureReasons(std::uint64_t mask)
        {
            nlohmann::json reasons = nlohmann::json::array();
            appendReason(reasons, mask,
                qualification_model::Failure_SessionNotActivated,
                "session_not_activated_on_render_thread");
            appendReason(reasons, mask,
                qualification_model::Failure_FeatureDisabled,
                "linear_lighting_disabled");
            appendReason(reasons, mask,
                qualification_model::Failure_GpuResourcesUnavailable,
                "gpu_resources_unavailable");
            appendReason(reasons, mask,
                qualification_model::Failure_GeometryProviderUnavailable,
                "geometry_provider_unavailable");
            appendReason(reasons, mask,
                qualification_model::Failure_ShaderDetoursUnowned,
                "shader_detours_unowned");
            appendReason(reasons, mask,
                qualification_model::Failure_DrawDetoursUnowned,
                "draw_detours_unowned");
            appendReason(reasons, mask,
                qualification_model::Failure_GeometryHookUnowned,
                "geometry_hook_unowned");
            appendReason(reasons, mask,
                qualification_model::Failure_ShaderContractsIncomplete,
                "replacement_contracts_incomplete");
            appendReason(reasons, mask,
                qualification_model::Failure_OriginalContractsIncomplete,
                "original_contracts_incomplete");
            appendReason(reasons, mask,
                qualification_model::Failure_NoGeometryCall,
                "no_world_geometry_call");
            appendReason(reasons, mask,
                qualification_model::Failure_GeometrySourceRejected,
                "geometry_source_rejected");
            appendReason(reasons, mask,
                qualification_model::Failure_NoGeometryUpdate,
                "no_fresh_geometry_upload");
            appendReason(reasons, mask,
                qualification_model::Failure_GeometryUpdateRejected,
                "geometry_upload_rejected");
            appendReason(reasons, mask,
                qualification_model::Failure_NoReplacementBind,
                "no_replacement_shader_bind");
            appendReason(reasons, mask,
                qualification_model::Failure_BindingStateMismatch,
                "replacement_bind_state_mismatch");
            appendReason(reasons, mask,
                qualification_model::Failure_NoVerifiedBinding,
                "no_verified_replacement_binding");
            appendReason(reasons, mask,
                qualification_model::Failure_NoReplacementDraw,
                "no_replacement_draw_submission");
            appendReason(reasons, mask,
                qualification_model::Failure_DrawStateMismatch,
                "pre_draw_state_mismatch");
            appendReason(reasons, mask,
                qualification_model::Failure_NoVerifiedDraw,
                "no_verified_replacement_draw");
            appendReason(reasons, mask,
                qualification_model::Failure_NoCommonVerifiedContract,
                "no_contract_completed_the_full_chain");
            appendReason(reasons, mask,
                qualification_model::Failure_ShaderContractCapacityExceeded,
                "shader_contract_capacity_exceeded");
            return reasons;
        }

        [[nodiscard]] nlohmann::json contractMaskWords(
            const linear_lighting::ContractMask& mask)
        {
            auto words = nlohmann::json::array();
            for (const auto word : mask) {
                words.push_back(word);
            }
            return words;
        }

        [[nodiscard]] Capture captureSession(const Session& session) noexcept
        {
            Capture capture{};
            capture.runtime = linear_lighting::Runtime::get().snapshot();
            capture.geometry = render::geometryHookSnapshot();
            capture.hooks = render::d3d11HookSnapshot();
            capture.d3d = render::d3d11QualificationSnapshot();
            capture.elapsedMilliseconds = GetTickCount64() -
                session.startedTickMilliseconds;
            capture.sample = {
                .sessionActivated = capture.d3d.sessionActive &&
                    capture.d3d.sessionId == session.d3dSessionId,
                .enabled = capture.runtime.enabled,
                .gpuResourcesReady = capture.runtime.gpuResourcesReady,
                .geometryProviderReady =
                    capture.runtime.geometryProviderReady,
                .shaderDetoursOwned =
                    capture.hooks.createPixelShaderDetourEnabled &&
                    capture.hooks.pixelShaderBindDetourEnabled,
                .drawDetoursOwned =
                    capture.hooks.qualificationDrawDetoursOwned,
                .geometryHookOwned = capture.geometry.vtableCellOwned,
                .expectedShaderContracts = static_cast<std::uint32_t>(
                    linear_lighting::Runtime::kShaderContractCount),
                .verifiedShaderContracts =
                    capture.runtime.verifiedShaderContracts,
                .matchingShaderContractMask =
                    capture.runtime.matchingShaderContractMask,
                .geometryCalls = delta(
                    capture.geometry.calls,
                    session.geometryCallsBaseline),
                .geometryAccepted = delta(
                    capture.geometry.acceptedUpdates,
                    session.geometryAcceptedBaseline),
                .geometrySourceRejected = delta(
                    capture.geometry.rejectedSources,
                    session.geometrySourceRejectedBaseline),
                .deepestGeometrySourceStage =
                    static_cast<std::uint32_t>(capture.geometry.deepestStage),
                .geometryUpdates = delta(
                    capture.runtime.geometryUpdates,
                    session.geometryUpdatesBaseline),
                .geometryUpdateRejects = delta(
                    capture.runtime.rejectedGeometryUpdates,
                    session.geometryUpdateRejectsBaseline),
                .replacementShaderBinds =
                    capture.d3d.replacementShaderBinds,
                .replacementDrawCalls = capture.d3d.replacementDrawCalls,
                .bindingStateFailures = capture.d3d.bindingStateFailures,
                .drawStateFailures = capture.d3d.drawStateFailures,
                .replacementContractMask =
                    capture.d3d.replacementContractMask,
                .bindingVerifiedContractMask =
                    capture.d3d.bindingVerifiedContractMask,
                .drawVerifiedContractMask =
                    capture.d3d.drawVerifiedContractMask,
            };
            return capture;
        }

        [[nodiscard]] nlohmann::json contractReport(
            const Capture& capture)
        {
            nlohmann::json contracts = nlohmann::json::array();
            for (std::size_t index = 0;
                 index < linear_lighting::Runtime::kShaderContractCount;
                 ++index) {
                contracts.push_back({
                    { "index", index },
                    { "name",
                        linear_lighting::Runtime::shaderContractName(index) },
                    { "originalObserved",
                        linear_lighting::contractBitSet(
                            capture.runtime.matchingShaderContractMask,
                            index) },
                    { "replacementBound",
                        linear_lighting::contractBitSet(
                            capture.d3d.replacementContractMask,
                            index) },
                    { "bindingVerified",
                        linear_lighting::contractBitSet(
                            capture.d3d.bindingVerifiedContractMask,
                            index) },
                    { "drawVerified",
                        linear_lighting::contractBitSet(
                            capture.d3d.drawVerifiedContractMask,
                            index) },
                });
            }
            return contracts;
        }

        [[nodiscard]] bool writeReport(
            const Session& session,
            const Capture& capture,
            const Evaluation& evaluation,
            bool worldLifecycleReached = true,
            const char* statusOverride = nullptr) noexcept
        {
            try {
                const auto& directory = logging::outputDirectory();
                if (directory.empty()) {
                    return false;
                }
                const auto reportPath = directory / kQualificationReportFileName;
                auto temporaryPath = reportPath;
                temporaryPath += L".tmp";

                const nlohmann::json report{
                    { "schemaVersion", 2 },
                    { "feature", "LinearLighting" },
                    { "contractMaskEncoding",
                        {
                            { "wordBits",
                                linear_lighting::kContractMaskWordBits },
                            { "wordCount",
                                linear_lighting::kContractMaskWordCount },
                            { "capacity",
                                linear_lighting::kContractMaskCapacity },
                            { "wordOrder", "least-significant-first" },
                        } },
                    { "status", statusOverride ? statusOverride :
                                                  statusName(evaluation.status) },
                    { "failureReasons",
                        failureReasons(evaluation.reasonMask) },
                    { "session",
                        {
                            { "processId", GetCurrentProcessId() },
                            { "generation", session.generation },
                            { "d3dSessionId", session.d3dSessionId },
                            { "trigger", session.trigger },
                            { "elapsedMilliseconds",
                                capture.elapsedMilliseconds },
                            { "worldLifecycleReached",
                                worldLifecycleReached },
                            { "renderThreadActivated",
                                capture.sample.sessionActivated },
                        } },
                    { "runtime",
                        {
                            { "enabled", capture.runtime.enabled },
                            { "gpuResourcesReady",
                                capture.runtime.gpuResourcesReady },
                            { "geometryProviderReady",
                                capture.runtime.geometryProviderReady },
                            { "verifiedShaderContracts",
                                capture.runtime.verifiedShaderContracts },
                            { "matchingShaderContractMaskWords",
                                contractMaskWords(capture.runtime
                                                      .matchingShaderContractMask) },
                            { "matchingShadersCreated",
                                capture.runtime.matchingShadersCreated },
                            { "trackedOriginalShaders",
                                capture.runtime.trackedOriginalShaders },
                            { "geometryCalls",
                                capture.sample.geometryCalls },
                            { "geometryAccepted",
                                capture.sample.geometryAccepted },
                            { "geometrySourceRejected",
                                capture.sample.geometrySourceRejected },
                            { "deepestGeometrySourceStage",
                                capture.sample.deepestGeometrySourceStage },
                            { "geometryUpdates",
                                capture.sample.geometryUpdates },
                            { "geometryUpdateRejects",
                                capture.sample.geometryUpdateRejects },
                            { "lastSourceEmissive",
                                capture.geometry.lastSourceEmissiveMultiplier },
                        } },
                    { "hooks",
                        {
                            { "createPixelShaderOwned",
                                capture.hooks
                                    .createPixelShaderDetourEnabled },
                            { "pixelShaderBindOwned",
                                capture.hooks.pixelShaderBindDetourEnabled },
                            { "qualificationDrawDetoursInstalled",
                                capture.hooks
                                    .qualificationDrawDetoursInstalled },
                            { "qualificationDrawDetoursOwned",
                                capture.hooks
                                    .qualificationDrawDetoursOwned },
                            { "geometryHookOwned",
                                capture.geometry.vtableCellOwned },
                        } },
                    { "renderProof",
                        {
                            { "replacementShaderBinds",
                                capture.d3d.replacementShaderBinds },
                            { "drawIndexedCalls",
                                capture.d3d.drawIndexedCalls },
                            { "drawCalls", capture.d3d.drawCalls },
                            { "drawIndexedInstancedCalls",
                                capture.d3d.drawIndexedInstancedCalls },
                            { "drawInstancedCalls",
                                capture.d3d.drawInstancedCalls },
                            { "replacementDrawCalls",
                                capture.d3d.replacementDrawCalls },
                            { "bindingStateChecks",
                                capture.d3d.bindingStateChecks },
                            { "bindingStateFailures",
                                capture.d3d.bindingStateFailures },
                            { "drawStateChecks",
                                capture.d3d.drawStateChecks },
                            { "drawStateFailures",
                                capture.d3d.drawStateFailures },
                            { "bindingsWithoutFreshGeometry",
                                capture.d3d
                                    .bindingsWithoutFreshGeometry },
                            { "drawsWithoutFreshGeometry",
                                capture.d3d.drawsWithoutFreshGeometry },
                            { "replacementContractMaskWords",
                                contractMaskWords(
                                    capture.d3d.replacementContractMask) },
                            { "bindingVerifiedContractMaskWords",
                                contractMaskWords(capture.d3d
                                                      .bindingVerifiedContractMask) },
                            { "drawVerifiedContractMaskWords",
                                contractMaskWords(
                                    capture.d3d.drawVerifiedContractMask) },
                            { "fullyVerifiedContractMaskWords",
                                contractMaskWords(
                                    evaluation.fullyVerifiedContractMask) },
                            { "lastBindingState",
                                capture.d3d.lastBindingState },
                            { "lastDrawState",
                                capture.d3d.lastDrawState },
                        } },
                    { "contracts", contractReport(capture) },
                };

                std::ofstream output(
                    temporaryPath,
                    std::ios::binary | std::ios::trunc);
                if (!output) {
                    return false;
                }
                output << report.dump(2) << '\n';
                output.flush();
                if (!output.good()) {
                    return false;
                }
                output.close();
                return MoveFileExW(
                           temporaryPath.c_str(),
                           reportPath.c_str(),
                           MOVEFILE_REPLACE_EXISTING |
                               MOVEFILE_WRITE_THROUGH) != FALSE;
            } catch (...) {
                return false;
            }
        }

        // F4SE lifecycle messages are the sole session producer. This worker
        // is the sole report-file owner and samples only atomic snapshots;
        // render callbacks never wait on this mutex or perform file I/O.
        class QualificationReporter final
        {
        public:
            static QualificationReporter& get() noexcept
            {
                static QualificationReporter instance;
                return instance;
            }

            void start() noexcept
            {
                try {
                    std::scoped_lock lock(mutex_);
                    if (worker_.joinable()) {
                        return;
                    }
                    const Session startupSession{
                        .startedTickMilliseconds = GetTickCount64(),
                        .trigger = "PluginLoad",
                    };
                    const auto startupCapture = captureSession(startupSession);
                    auto startupEvaluation = qualification_model::evaluate(
                        startupCapture.sample,
                        false);
                    startupEvaluation.status = Status::waiting;
                    publishPublicStatus(
                        LinearLightingQualificationState::waitingForWorld,
                        startupSession,
                        startupCapture,
                        startupEvaluation,
                        false);
                    if (!writeReport(
                            startupSession,
                            startupCapture,
                            startupEvaluation,
                            false,
                            "waiting_for_world")) {
                        logging::error(
                            "Linear Lighting qualification startup report could not be written.");
                    }
                    worker_ = std::jthread(
                        [this](std::stop_token stopToken) {
                            run(stopToken);
                        });
                } catch (...) {
                    logging::error(
                        "Linear Lighting qualification reporter thread failed to start.");
                }
            }

            void beginSession(const char* trigger) noexcept
            {
                try {
                    const auto runtime =
                        linear_lighting::Runtime::get().snapshot();
                    const auto geometry = render::geometryHookSnapshot();
                    const auto d3dSessionId =
                        render::beginD3D11QualificationSession(
                            runtime.geometryUpdates);
                    std::scoped_lock lock(mutex_);
                    session_ = {
                        .generation = session_.generation + 1,
                        .d3dSessionId = d3dSessionId,
                        .startedTickMilliseconds = GetTickCount64(),
                        .geometryCallsBaseline = geometry.calls,
                        .geometryAcceptedBaseline = geometry.acceptedUpdates,
                        .geometrySourceRejectedBaseline =
                            geometry.rejectedSources,
                        .geometryUpdatesBaseline = runtime.geometryUpdates,
                        .geometryUpdateRejectsBaseline =
                            runtime.rejectedGeometryUpdates,
                        .trigger = trigger ? trigger : "unknown",
                    };
                    publishArmedSession(session_);
                    logging::info(
                        "Linear Lighting automated qualification session {} armed at world lifecycle '{}' (D3D session {}, timeout {} ms).",
                        session_.generation,
                        session_.trigger,
                        session_.d3dSessionId,
                        kQualificationTimeoutMilliseconds);
                } catch (...) {
                    logging::error(
                        "Linear Lighting qualification session failed to arm.");
                }
            }

            ~QualificationReporter()
            {
                if (worker_.joinable()) {
                    worker_.request_stop();
                    worker_.join();
                }
            }

            QualificationReporter(const QualificationReporter&) = delete;
            QualificationReporter& operator=(const QualificationReporter&) =
                delete;

        private:
            QualificationReporter() = default;

            [[nodiscard]] Session currentSession() noexcept
            {
                std::scoped_lock lock(mutex_);
                return session_;
            }

            void run(std::stop_token stopToken) noexcept
            {
                std::uint64_t finalGeneration{};
                std::uint64_t runningReportGeneration{};
                while (!stopToken.stop_requested()) {
                    const auto session = currentSession();
                    if (session.generation == 0 ||
                        session.generation == finalGeneration) {
                        std::this_thread::sleep_for(
                            kQualificationPollInterval);
                        continue;
                    }

                    const auto capture = captureSession(session);
                    const auto timedOut = capture.elapsedMilliseconds >=
                        kQualificationTimeoutMilliseconds;
                    const auto evaluation = qualification_model::evaluate(
                        capture.sample,
                        timedOut);
                    const auto publicQualificationState =
                        !capture.sample.sessionActivated ?
                        LinearLightingQualificationState::running :
                        (evaluation.status == Status::passed ?
                                LinearLightingQualificationState::passed :
                                (evaluation.status == Status::failed ?
                                        LinearLightingQualificationState::failed :
                                        LinearLightingQualificationState::running));
                    publishPublicStatus(
                        publicQualificationState,
                        session,
                        capture,
                        evaluation,
                        true);
                    if (runningReportGeneration != session.generation) {
                        auto runningEvaluation = evaluation;
                        runningEvaluation.status = Status::waiting;
                        if (!writeReport(
                                session,
                                capture,
                                runningEvaluation)) {
                            logging::error(
                                "Linear Lighting qualification running report could not be written.");
                        }
                        runningReportGeneration = session.generation;
                    }

                    if (capture.sample.sessionActivated &&
                        evaluation.status != Status::waiting) {
                        if (!writeReport(session, capture, evaluation)) {
                            logging::error(
                                "Linear Lighting qualification final report could not be written.");
                        } else {
                            logging::info(
                                "Linear Lighting automated qualification {} for session {} after {} ms (reasonMask=0x{:X}, fullyVerifiedContractMaskWords=[0x{:016X},0x{:016X},0x{:016X},0x{:016X},0x{:016X}]).",
                                statusName(evaluation.status),
                                session.generation,
                                capture.elapsedMilliseconds,
                                evaluation.reasonMask,
                                evaluation.fullyVerifiedContractMask[0],
                                evaluation.fullyVerifiedContractMask[1],
                                evaluation.fullyVerifiedContractMask[2],
                                evaluation.fullyVerifiedContractMask[3],
                                evaluation.fullyVerifiedContractMask[4]);
                        }
                        render::endD3D11QualificationSession(
                            session.d3dSessionId);
                        finalGeneration = session.generation;
                    } else if (timedOut) {
                        const auto timeoutEvaluation =
                            qualification_model::evaluate(
                                capture.sample,
                                true);
                        publishPublicStatus(
                            LinearLightingQualificationState::failed,
                            session,
                            capture,
                            timeoutEvaluation,
                            true);
                        if (!writeReport(
                                session,
                                capture,
                                timeoutEvaluation)) {
                            logging::error(
                                "Linear Lighting qualification timeout report could not be written.");
                        } else {
                            logging::error(
                                "Linear Lighting automated qualification failed for session {} after timeout (reasonMask=0x{:X}).",
                                session.generation,
                                timeoutEvaluation.reasonMask);
                        }
                        render::endD3D11QualificationSession(
                            session.d3dSessionId);
                        finalGeneration = session.generation;
                    }
                    std::this_thread::sleep_for(kQualificationPollInterval);
                }
            }

            std::mutex mutex_;
            Session session_{};
            std::jthread worker_;
        };
    }

    void startLinearLightingQualificationReporter() noexcept
    {
        QualificationReporter::get().start();
    }

    void beginLinearLightingQualificationSession(const char* trigger) noexcept
    {
        QualificationReporter::get().beginSession(trigger);
    }

    LinearLightingQualificationSnapshot
    linearLightingQualificationSnapshot() noexcept
    {
        const auto state = publicState.load(std::memory_order_acquire);
        return {
            .state = state,
            .generation = publicGeneration.load(std::memory_order_relaxed),
            .elapsedMilliseconds = publicElapsedMilliseconds.load(
                std::memory_order_relaxed),
            .reasonMask = publicReasonMask.load(std::memory_order_relaxed),
            .fullyVerifiedContracts = publicFullyVerifiedContracts.load(
                std::memory_order_relaxed),
            .expectedContracts = publicExpectedContracts.load(
                std::memory_order_relaxed),
            .worldLifecycleReached = publicWorldLifecycleReached.load(
                std::memory_order_relaxed),
            .renderThreadActivated = publicRenderThreadActivated.load(
                std::memory_order_relaxed),
        };
    }

    const char* linearLightingQualificationStateName(
        LinearLightingQualificationState state) noexcept
    {
        switch (state) {
        case LinearLightingQualificationState::running:
            return "running";
        case LinearLightingQualificationState::passed:
            return "passed";
        case LinearLightingQualificationState::failed:
            return "failed";
        default:
            return "waiting_for_world";
        }
    }
}
