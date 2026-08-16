#include "PCH.h"

#include "Features/basic_wetness/BasicWetnessRuntime.h"
#include "Features/basic_wetness/BasicWetnessSettingsStore.h"
#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/cloud_shadows/CloudShadowSettingsStore.h"
#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "Features/dlaa/DlaaD3D11Hooks.h"
#include "Features/dlaa/DlaaEngineHooks.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/DlaaSettingsStore.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h"
#include "Features/hair_specular/HairSpecularRuntime.h"
#include "Features/hair_specular/HairSpecularSettingsStore.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/ibl/IblSettingsStore.h"
#include "Features/complex_materials/ComplexParallaxSettingsStore.h"
#include "Features/linear_lighting/DFTiledPointLightHook.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "Features/native_shadows/NativeShadowRuntime.h"
#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h"
#include "Features/vanilla_fixes/VanillaFixesRuntime.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/wrapped_grass/WrappedGrassRuntime.h"
#include "Features/wrapped_grass/WrappedGrassSettingsStore.h"
#include "diagnostics/LinearLightingQualification.h"
#include "render/BSDFPrePassShaderHook.h"
#include "render/BSLightingGeometryHook.h"
#include "render/D3D11Hooks.h"
#include "support/Logger.h"
#include "settings/SharedSettingsRuntime.h"

extern "C" __declspec(dllexport) constinit F4SE::PluginVersionData F4SEPlugin_Version = []() noexcept {
    F4SE::PluginVersionData version{};
    version.PluginName("FO4VR Community Shaders");
    version.PluginVersion(REL::Version(0, 2, 0));
    version.AuthorName("FO4VR Community Shaders Port");
    return version;
}();

namespace
{
    void reportPluginBoundaryFailure(
        const char* boundary,
        const char* detail) noexcept
    {
        char message[1024]{};
        if (detail) {
            std::snprintf(
                message,
                sizeof(message),
                "FO4VR Community Shaders: %s failed: %s\n",
                boundary,
                detail);
        } else {
            std::snprintf(
                message,
                sizeof(message),
                "FO4VR Community Shaders: %s failed with an unknown exception.\n",
                boundary);
        }
        OutputDebugStringA(message);
        community_shaders::logging::critical("{}", message);
    }

    void F4SEAPI onF4SEMessage(
        F4SE::MessagingInterface::Message* message) noexcept
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case F4SE::MessagingInterface::kPostPostLoad:
        {
            const auto d3d = community_shaders::render::d3d11HookSnapshot();
            const auto dlaa = community_shaders::dlaa::Runtime::get().snapshot();
            community_shaders::logging::info(
                "F4SE PostPostLoad: D3D importHook={}, deviceCaptured={}, deviceHooks={}, createCalls={}; DLAA requested={}, Streamline initialized={}.",
                d3d.deviceCreationImportInstalled,
                d3d.deviceCaptured,
                d3d.deviceHooksInstalled,
                d3d.deviceCreationCalls,
                dlaa.settings.enabled,
                dlaa.streamline.initialized);
            break;
        }
        case F4SE::MessagingInterface::kGameDataReady:
        {
            community_shaders::native_shadows::onGameDataReady();
            (void)community_shaders::render::
                validateD3D11ShaderHooks("GameDataReady");
            (void)community_shaders::render::
                validateBSLightingGeometryHook("GameDataReady");
            (void)community_shaders::render::
                validateBSDFPrePassShaderHook("GameDataReady");
            (void)community_shaders::linear_lighting::
                validateDFTiledPointLightHook("GameDataReady");
            (void)community_shaders::dlaa::validateEngineHooks(
                "GameDataReady");
            (void)community_shaders::dlaa::validateD3D11Hooks(
                "GameDataReady");
            const auto linearLighting =
                community_shaders::linear_lighting::Runtime::get().snapshot();
            const auto geometry =
                community_shaders::render::geometryHookSnapshot();
            const auto d3d =
                community_shaders::render::d3d11HookSnapshot();
            const auto pointLight = community_shaders::linear_lighting::
                dFTiledPointLightHookSnapshot();
            const auto dlaa =
                community_shaders::dlaa::Runtime::get().snapshot();
            const auto dlaaEngine =
                community_shaders::dlaa::engineHookSnapshot();
            const auto dlaaD3d =
                community_shaders::dlaa::d3d11HookSnapshot();
            community_shaders::logging::info(
                "F4SE GameDataReady: Linear Lighting enabled={}, gpuReady={}, geometryReady={}, matchingShaders={}, trackedShaders={}, grassVsMask=0x{:02X}, grassVsCreated={}, grassVsTracked={}, grassClassSelections={}, ambientContractMask=0x{:010X}, ambientReadyMask=0x{:010X}, ambientShaders={}, ambientTracked={}, ambientBuilds={}, ambientBuildFailures={}, psBindCalls={}, shaderSelections={}, replacementBinds={}, ambientReplacementBinds={}, d3dBindDetourEnabled={}, techniqueCellOwned={}, geometryCellOwned={}, dFLightProducerOwned={}, techniqueCalls={}, geometryCalls={}, geometryUpdates={}, geometrySourceRejects={}, deepestGeometrySourceStage={}, ambientDescriptors={}, directionalDescriptors={}, ambientTransformPrepared={}, directionalPowModified={}, pointDetourOwned={}, pointGammaLoadsOwned={}, pointCalls={}, pointModified={}, pointGamma={}, pointMultiplier={}.",
                linearLighting.enabled,
                linearLighting.gpuResourcesReady,
                linearLighting.geometryProviderReady,
                linearLighting.matchingShadersCreated,
                linearLighting.trackedOriginalShaders,
                linearLighting.matchingGrassVertexShaderIdentityMask,
                linearLighting.matchingGrassVertexShadersCreated,
                linearLighting.trackedGrassVertexShaders,
                linearLighting.grassVertexClassSelections,
                linearLighting.matchingDFLightAmbientContractMask,
                linearLighting.readyDFLightAmbientContractMask,
                linearLighting.matchingDFLightAmbientShaders,
                linearLighting.trackedDFLightAmbientShaders,
                linearLighting.dFLightAmbientReplacementBuilds,
                linearLighting.dFLightAmbientReplacementFailures,
                d3d.pixelShaderBindCalls,
                linearLighting.shaderSelectionCalls,
                linearLighting.replacementBinds,
                linearLighting.dFLightAmbientReplacementBinds,
                d3d.pixelShaderBindDetourEnabled,
                geometry.techniqueVtableCellOwned,
                geometry.geometryVtableCellOwned,
                geometry.dFLightProducerCallsitesOwned,
                geometry.techniqueCalls,
                geometry.calls,
                geometry.acceptedUpdates,
                geometry.rejectedSources,
                static_cast<std::uint32_t>(geometry.deepestStage),
                geometry.ambientDescriptors,
                geometry.directionalDescriptors,
                geometry.ambientTransformPrepared,
                geometry.directionalPowModified,
                pointLight.detourOwned,
                pointLight.gammaLoadsOwned,
                pointLight.completedCalls,
                pointLight.modifiedCalls,
                pointLight.activeGamma,
                pointLight.activeColorMultiplier);
            community_shaders::logging::info(
                "F4SE GameDataReady: DLAA requested={}, operational={}, engineHooksOwned={}, mapUnmapOwned={}, cameraQualified={}, cameraBinding={}, renderResourcesQualified={}, Streamline initialized={}, deviceBound={}, swapchainUpgraded={}, DLSS loaded={}, supported={}, functionsBound={}, cameraFrames={}, containingMaps={}, identityMatches={}, validationFailures={}, preCalls={}, postCalls={}, stereoEvaluations={}, evaluationFailures={}, committedFrames={}.",
                dlaa.settings.enabled,
                dlaa.operational,
                dlaaEngine.owned,
                dlaaD3d.owned,
                dlaa.cameraBufferQualified,
                dlaa.cameraBindingObserved,
                dlaa.renderResourcesQualified,
                dlaa.streamline.initialized,
                dlaa.streamline.deviceBound,
                dlaa.streamline.swapChainUpgraded,
                dlaa.streamline.featureLoaded,
                dlaa.streamline.featureSupported,
                dlaa.streamline.featureFunctionsBound,
                dlaa.mappedCameraFrames,
                dlaa.cameraMapCandidates,
                dlaa.cameraIdentityMatches,
                dlaa.cameraValidationFailures,
                dlaa.preRenderCalls,
                dlaa.postRenderCalls,
                dlaa.stereoEvaluations,
                dlaa.stereoEvaluationFailures,
                dlaa.committedFrames);
            break;
        }
        case F4SE::MessagingInterface::kPostLoadGame:
            community_shaders::native_shadows::onWorldReady("PostLoadGame");
            (void)community_shaders::render::
                validateD3D11ShaderHooks("GameSessionReady");
            community_shaders::ibl::Runtime::get()
                .beginWorldCaptureProbeSession();
            community_shaders::diagnostics::
                beginLinearLightingQualificationSession("PostLoadGame");
            community_shaders::dlaa::Runtime::get().beginQualificationSession(
                "PostLoadGame");
            break;
        case F4SE::MessagingInterface::kNewGame:
            community_shaders::native_shadows::onWorldReady("NewGame");
            (void)community_shaders::render::
                validateD3D11ShaderHooks("GameSessionReady");
            community_shaders::ibl::Runtime::get()
                .beginWorldCaptureProbeSession();
            community_shaders::diagnostics::
                beginLinearLightingQualificationSession("NewGame");
            community_shaders::dlaa::Runtime::get().beginQualificationSession(
                "NewGame");
            break;
        default:
            break;
        }
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
    const F4SE::QueryInterface* a_f4se,
    F4SE::PluginInfo* a_info) noexcept
{
    try {
        if (!a_f4se || !a_info) {
            return false;
        }

        community_shaders::logging::init();
        community_shaders::logging::info(
            "=== FO4VR Community Shaders v0.2.0 query ===");

        a_info->infoVersion = F4SE::PluginInfo::kVersion;
        a_info->name = "FO4VR Community Shaders";
        a_info->version = 200;

        if (a_f4se->IsEditor()) {
            community_shaders::logging::critical(
                "Editor runtime is unsupported.");
            return false;
        }
        if (!REL::Module::IsVR()) {
            community_shaders::logging::critical(
                "Fallout 4 VR runtime is required.");
            return false;
        }

        // F4SEVR exposes its flat-compatible loader API version here. This is
        // a different version domain from Fallout4VR.exe itself.
        const auto requiredRuntime = F4SE::RUNTIME_1_10_138;
        if (a_f4se->RuntimeVersion() < requiredRuntime) {
            community_shaders::logging::critical(
                "Unsupported F4SE compatibility runtime {} (need >= {}).",
                a_f4se->RuntimeVersion().string(),
                requiredRuntime.string());
            return false;
        }

        const auto executableVersion = REL::Module::get().version();
        if (executableVersion != F4SE::RUNTIME_VR_1_2_72) {
            community_shaders::logging::critical(
                "Only Fallout4VR.exe 1.2.72 is supported; executable={}, F4SE compatibility runtime={}.",
                executableVersion.string(),
                a_f4se->RuntimeVersion().string());
            return false;
        }

        community_shaders::logging::info(
            "Runtime gate passed: Fallout4VR.exe {}, F4SE compatibility runtime {}.",
            executableVersion.string(),
            a_f4se->RuntimeVersion().string());
        return true;
    } catch (const std::exception& error) {
        reportPluginBoundaryFailure("F4SEPlugin_Query", error.what());
        return false;
    } catch (...) {
        reportPluginBoundaryFailure("F4SEPlugin_Query", nullptr);
        return false;
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(
    const F4SE::LoadInterface* a_f4se) noexcept
{
    try {
        if (!a_f4se) {
            return false;
        }
        F4SE::Init(a_f4se, false);

        const auto settings =
            community_shaders::linear_lighting::loadSettings();
        const auto dlaaSettings = community_shaders::dlaa::loadSettings();
        const auto filmicTonemappingSettings =
            community_shaders::filmic_tonemapping::loadSettings();
        const auto iblSettings = community_shaders::ibl::loadSettings();
        const auto contactShadowSettings =
            community_shaders::contact_shadows::loadSettings();
        const auto complexMaterialSettings =
            community_shaders::complex_materials::loadSettings();
        const auto wrappedGrassSettings =
            community_shaders::wrapped_grass::loadSettings();
        const auto hairSpecularSettings =
            community_shaders::hair_specular::loadSettings();
        const auto subsurfaceScatteringSettings =
            community_shaders::subsurface_scattering::loadSettings();
        const auto basicWetnessSettings =
            community_shaders::basic_wetness::loadSettings();
        const auto cloudShadowSettings =
            community_shaders::cloud_shadows::loadSettings();
        const auto vanillaFixesSettings =
            community_shaders::vanilla_fixes::loadSettings();
        const auto nativeShadowSettings =
            community_shaders::native_shadows::loadSettings();
        community_shaders::linear_lighting::Runtime::get().applySettings(
            settings);
        community_shaders::dlaa::Runtime::get().applySettings(dlaaSettings);
        community_shaders::filmic_tonemapping::Runtime::get().applySettings(
            filmicTonemappingSettings);
        community_shaders::linear_lighting::Runtime::get().
            applyComplexParallaxSettings(complexMaterialSettings);
        community_shaders::ibl::Runtime::get().applySettings(iblSettings);
        community_shaders::contact_shadows::Runtime::get().applySettings(
            contactShadowSettings);
        community_shaders::cloud_shadows::Runtime::get().applySettings(
            cloudShadowSettings);
        community_shaders::wrapped_grass::Runtime::get().applySettings(
            wrappedGrassSettings);
        community_shaders::hair_specular::Runtime::get().applySettings(
            hairSpecularSettings);
        community_shaders::subsurface_scattering::Runtime::get().applySettings(
            subsurfaceScatteringSettings);
        community_shaders::basic_wetness::Runtime::get().applySettings(
            basicWetnessSettings);
        if (!community_shaders::native_shadows::startRuntime(
                nativeShadowSettings)) {
            community_shaders::logging::warn(
                "Native Shadows rejected one or more FO4VR engine contracts; unknown boundaries remain vanilla and extended-cascade masks remain fail-closed.");
        }
        if (!community_shaders::vanilla_fixes::startRuntime(
                vanillaFixesSettings)) {
            community_shaders::logging::warn(
                "Vanilla Fixes engine-gate contract was rejected; shader fixes remain available, but engine-gate ownership stays fail-closed.");
        }
        if (!community_shaders::render::installEarlyD3D11Hooks()) {
            community_shaders::logging::warn(
                "Verified D3D11 bootstrap was not installed; plugin remains loaded but all rendering stays vanilla.");
        }
        if (!community_shaders::dlaa::installEngineHooks()) {
            community_shaders::logging::warn(
                "Verified FO4VR DLAA engine boundaries were not installed; vanilla TAA remains active.");
        }
        if (!community_shaders::render::installBSLightingGeometryHook()) {
            community_shaders::logging::warn(
                "Verified BSDF lighting geometry hook was not installed; Linear Lighting replacement remains fail-closed.");
        }
        if (!community_shaders::render::installBSDFPrePassShaderHook()) {
            community_shaders::logging::warn(
                "Verified BSDFPrePass descriptor hook was not installed; complex environment materials remain fail-closed.");
        }

        const auto* messaging = F4SE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(onF4SEMessage)) {
            community_shaders::logging::critical(
                "F4SE messaging registration failed.");
            return false;
        }
        community_shaders::diagnostics::
            startLinearLightingQualificationReporter();
        if (!community_shaders::shared_settings::startMonitor()) {
            community_shaders::logging::warn(
                "Shared Community Shaders INI monitor did not start; startup settings remain active, but DevMenu changes require the next launch.");
        }

        community_shaders::logging::info(
            "FO4VR Community Shaders loaded; persisted upscaling enabled={}, mode={}, modelPreset={}, sharpening={}, sharpness={}; Linear Lighting enabled={}, Image Based Lighting enabled={}, diffuse IBL enabled={}, diffuse level={}, Contact Shadows enabled={}, samples={}, Wrapped Grass Lighting enabled={}, wrap amount={}, Hair Specular enabled={}, multiplier={}, Subsurface Scattering enabled={}, strength={}, Basic Wetness enabled={}, wetness={}, Cloud Shadows enabled={}, opacity={}, complex parallax enabled={}, parallax quality={}, Native Shadows enabled={}, four cascades={}, tiled deferred lighting={}, fixed shadow distance={}, Vanilla Fixes enabled={}, focus shadows={}, and replacements remain fail-closed until their verified render providers are ready.",
            dlaaSettings.enabled,
            community_shaders::dlaa::modeName(dlaaSettings.mode),
            static_cast<std::uint32_t>(dlaaSettings.modelPreset),
            dlaaSettings.sharpening,
            dlaaSettings.sharpness,
            settings.enabled,
            iblSettings.enabled,
            iblSettings.diffuseEnabled,
            iblSettings.diffuseLevel,
            contactShadowSettings.enabled,
            contactShadowSettings.sampleCount,
            wrappedGrassSettings.enabled,
            wrappedGrassSettings.wrapAmount,
            hairSpecularSettings.enabled,
            hairSpecularSettings.specularMultiplier,
            subsurfaceScatteringSettings.enabled,
            subsurfaceScatteringSettings.strength,
            basicWetnessSettings.enabled,
            basicWetnessSettings.wetness,
            cloudShadowSettings.enabled,
            cloudShadowSettings.opacity,
            complexMaterialSettings.parallaxEnabled,
            complexMaterialSettings.parallaxQuality,
            nativeShadowSettings.enabled,
            nativeShadowSettings.extendedDirectionalCascades,
            nativeShadowSettings.tiledDeferredLighting,
            nativeShadowSettings.directionalShadowDistance,
            vanillaFixesSettings.enabled,
            vanillaFixesSettings.focusShadows);
        return true;
    } catch (const std::exception& error) {
        reportPluginBoundaryFailure("F4SEPlugin_Load", error.what());
        return false;
    } catch (...) {
        reportPluginBoundaryFailure("F4SEPlugin_Load", nullptr);
        return false;
    }
}
