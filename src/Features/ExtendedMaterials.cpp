#include "Features/ExtendedMaterials.h"

#include <imgui.h>

// Shader type constants matching BSShader::Type enum
static constexpr uint32_t kBSLightingShader = 8;

bool ExtendedMaterials::HasShaderDefine(uint32_t shaderType)
{
    return shaderType == kBSLightingShader;
}

void ExtendedMaterials::SetupResources()
{
    spdlog::info("ExtendedMaterials::SetupResources — creating constant buffer");
    settingsCB = std::make_unique<ConstantBuffer>(sizeof(ExtendedMaterialsCBData));

    if (!settingsCB || !settingsCB->IsValid()) {
        spdlog::error("ExtendedMaterials::SetupResources — failed to create constant buffer");
    }
}

void ExtendedMaterials::Prepass()
{
    if (!settingsCB || !settingsCB->IsValid()) {
        return;
    }

    ExtendedMaterialsCBData data{};
    data.enablePOM = enablePOM ? 1u : 0u;
    data.enableShadows = enableShadows ? 1u : 0u;
    data.maxSteps = maxSteps;
    data.heightScaleMult = heightScaleMult;

    settingsCB->Update(&data, sizeof(data));
}

void ExtendedMaterials::OnSetupGeometry(void* /*renderPass*/)
{
    if (!settingsCB || !settingsCB->IsValid()) {
        return;
    }

    ScopedD3DState guard;
    settingsCB->PSBind(5);
}

void ExtendedMaterials::DrawSettings()
{
    ImGui::Checkbox("Enable POM", &enablePOM);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable Parallax Occlusion Mapping for surfaces with a height map.");
    }

    ImGui::Checkbox("Enable POM Shadows", &enableShadows);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Cast self-shadowing within the parallax surface.");
    }

    int steps = static_cast<int>(maxSteps);
    if (ImGui::SliderInt("Max Steps (0 = Auto)", &steps, 0, 32)) {
        maxSteps = static_cast<uint32_t>(steps);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Maximum ray-march steps for POM. "
            "0 selects an automatic quality level based on view angle.");
    }

    ImGui::SliderFloat("Height Scale Multiplier", &heightScaleMult, 0.1f, 3.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scales the parallax depth effect. 1.0 is the author-intended depth.");
    }
}

void ExtendedMaterials::SaveSettings(nlohmann::json& j)
{
    j["enablePOM"] = enablePOM;
    j["enableShadows"] = enableShadows;
    j["maxSteps"] = maxSteps;
    j["heightScaleMult"] = heightScaleMult;
}

void ExtendedMaterials::LoadSettings(const nlohmann::json& j)
{
    if (j.contains("enablePOM")) {
        enablePOM = j["enablePOM"].get<bool>();
    }
    if (j.contains("enableShadows")) {
        enableShadows = j["enableShadows"].get<bool>();
    }
    if (j.contains("maxSteps")) {
        maxSteps = j["maxSteps"].get<uint32_t>();
    }
    if (j.contains("heightScaleMult")) {
        heightScaleMult = j["heightScaleMult"].get<float>();
    }
}
