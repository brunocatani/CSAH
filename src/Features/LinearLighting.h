#pragma once
#include "Feature.h"
#include "Buffer.h"

class LinearLighting : public Feature {
public:
    std::string GetName() override { return "Linear Lighting"; }
    std::string GetShortName() override { return "LinearLighting"; }
    std::string_view GetShaderDefineName() override { return "LINEAR_LIGHTING"; }
    bool HasShaderDefine(uint32_t shaderType) override;
    bool IsCore() override { return true; }

    void SetupResources() override;
    void Prepass() override;
    void OnSetupGeometry(void* renderPass) override;
    void DrawSettings() override;
    void SaveSettings(nlohmann::json& j) override;
    void LoadSettings(const nlohmann::json& j) override;

    float gamma = 2.2f;
    bool useExactSRGB = false;

private:
    struct alignas(16) LinearLightingCBData {
        float gamma;
        float useExact;
        float pad[2];
    };
    std::unique_ptr<ConstantBuffer> perFeatureCB;
};
