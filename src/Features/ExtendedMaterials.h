#pragma once
#include "Feature.h"
#include "Buffer.h"

class ExtendedMaterials : public Feature {
public:
    std::string GetName() override { return "Extended Materials"; }
    std::string GetShortName() override { return "ExtendedMaterials"; }
    std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
    bool HasShaderDefine(uint32_t shaderType) override;
    bool IsCore() override { return true; }

    void SetupResources() override;
    void Prepass() override;
    void OnSetupGeometry(void* renderPass) override;
    void DrawSettings() override;
    void SaveSettings(nlohmann::json& j) override;
    void LoadSettings(const nlohmann::json& j) override;

    bool enablePOM = true;
    bool enableShadows = true;
    uint32_t maxSteps = 0;
    float heightScaleMult = 1.0f;

private:
    struct alignas(16) ExtendedMaterialsCBData {
        uint32_t enablePOM;
        uint32_t enableShadows;
        uint32_t maxSteps;
        float heightScaleMult;
    };
    std::unique_ptr<ConstantBuffer> settingsCB;
};
