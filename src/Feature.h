#pragma once
#include "PCH.h"

struct Feature {
    virtual ~Feature() = default;

    // Metadata
    virtual std::string GetName() = 0;
    virtual std::string GetShortName() = 0;
    virtual std::string_view GetShaderDefineName() { return ""; }
    virtual bool HasShaderDefine(uint32_t /*shaderType*/) { return false; }
    virtual bool IsCore() { return false; }

    // Lifecycle
    virtual void SetupResources() {}
    virtual void Reset() {}
    virtual void Prepass() {}

    // Hook callbacks
    virtual void OnSetupGeometry(void* /*renderPass*/) {}
    virtual void OnSetupMaterial(void* /*material*/) {}
    virtual void OnDeferredPass() {}
    virtual void OnPostProcess() {}
    virtual void OnPreTonemap() {}

    // Settings
    virtual void DrawSettings() {}
    virtual void SaveSettings(nlohmann::json& /*j*/) {}
    virtual void LoadSettings(const nlohmann::json& /*j*/) {}

    bool enabled = true;
    bool loaded = false;

    // Registry
    static std::vector<Feature*>& GetFeatureList();
    static void RegisterFeature(Feature* feature);
    static void InitializeAll();
    static void ResetAll();
    static void PrepassAll();

    // Settings persistence
    static void SaveAllSettings(const std::filesystem::path& path);
    static void LoadAllSettings(const std::filesystem::path& path);
};
