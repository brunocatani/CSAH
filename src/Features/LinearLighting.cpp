#include "Features/LinearLighting.h"

#include <imgui.h>

// Shader type constants matching BSShader::Type enum
static constexpr uint32_t kBSGrassShader = 6;
static constexpr uint32_t kBSLightingShader = 8;

bool LinearLighting::HasShaderDefine(uint32_t shaderType)
{
    return shaderType == kBSLightingShader || shaderType == kBSGrassShader;
}

void LinearLighting::SetupResources()
{
    spdlog::info("LinearLighting::SetupResources — creating constant buffer");
    perFeatureCB = std::make_unique<ConstantBuffer>(sizeof(LinearLightingCBData));

    if (!perFeatureCB || !perFeatureCB->IsValid()) {
        spdlog::error("LinearLighting::SetupResources — failed to create constant buffer");
    }
}

void LinearLighting::Prepass()
{
    if (!perFeatureCB || !perFeatureCB->IsValid()) {
        return;
    }

    LinearLightingCBData data{};
    data.gamma = gamma;
    data.useExact = useExactSRGB ? 1.0f : 0.0f;
    data.pad[0] = 0.0f;
    data.pad[1] = 0.0f;

    perFeatureCB->Update(&data, sizeof(data));
}

void LinearLighting::OnSetupGeometry(void* /*renderPass*/)
{
    if (!perFeatureCB || !perFeatureCB->IsValid()) {
        return;
    }

    ScopedD3DState guard;
    perFeatureCB->VSBind(4);
    perFeatureCB->PSBind(4);
}

void LinearLighting::DrawSettings()
{
    ImGui::SliderFloat("Gamma", &gamma, 1.8f, 2.6f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("sRGB standard is 2.2. Lower values brighten, higher values darken.");
    }

    ImGui::Checkbox("Use Exact sRGB Curve", &useExactSRGB);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, uses the piecewise sRGB transfer function "
            "instead of the simple pow(x, gamma) approximation.");
    }
}

void LinearLighting::SaveSettings(nlohmann::json& j)
{
    j["gamma"] = gamma;
    j["useExactSRGB"] = useExactSRGB;
}

void LinearLighting::LoadSettings(const nlohmann::json& j)
{
    if (j.contains("gamma")) {
        gamma = j["gamma"].get<float>();
    }
    if (j.contains("useExactSRGB")) {
        useExactSRGB = j["useExactSRGB"].get<bool>();
    }
}
