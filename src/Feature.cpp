#include "Feature.h"

std::vector<Feature*>& Feature::GetFeatureList() {
    static std::vector<Feature*> features;
    return features;
}

void Feature::RegisterFeature(Feature* feature) {
    if (!feature) {
        spdlog::warn("Feature::RegisterFeature called with null pointer");
        return;
    }
    spdlog::info("Registering feature: {}", feature->GetName());
    GetFeatureList().push_back(feature);
}

void Feature::InitializeAll() {
    spdlog::info("Initializing {} feature(s)...", GetFeatureList().size());

    for (auto* feature : GetFeatureList()) {
        if (!feature->enabled) {
            spdlog::info("Feature '{}' is disabled, skipping initialization", feature->GetName());
            continue;
        }
        try {
            spdlog::info("Setting up resources for feature '{}'", feature->GetName());
            feature->SetupResources();
            feature->loaded = true;
            spdlog::info("Feature '{}' initialized successfully", feature->GetName());
        } catch (const std::exception& e) {
            spdlog::error("Failed to initialize feature '{}': {}", feature->GetName(), e.what());
            feature->loaded = false;
        } catch (...) {
            spdlog::error("Failed to initialize feature '{}': unknown error", feature->GetName());
            feature->loaded = false;
        }
    }

    auto loadedCount = std::ranges::count_if(GetFeatureList(), [](const Feature* f) { return f->loaded; });
    spdlog::info("Feature initialization complete: {}/{} loaded", loadedCount, GetFeatureList().size());
}

void Feature::ResetAll() {
    for (auto* feature : GetFeatureList()) {
        if (feature->loaded) {
            feature->Reset();
        }
    }
}

void Feature::PrepassAll() {
    for (auto* feature : GetFeatureList()) {
        if (feature->loaded) {
            feature->Prepass();
        }
    }
}

void Feature::SaveAllSettings(const std::filesystem::path& path) {
    nlohmann::json root;

    for (auto* feature : GetFeatureList()) {
        nlohmann::json featureJson;
        featureJson["enabled"] = feature->enabled;
        feature->SaveSettings(featureJson);
        root[feature->GetShortName()] = featureJson;
    }

    try {
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Failed to open settings file for writing: {}", path.string());
            return;
        }
        file << root.dump(4);
        spdlog::info("Settings saved to {}", path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to save settings: {}", e.what());
    }
}

void Feature::LoadAllSettings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::info("No settings file found at {}, using defaults", path.string());
        return;
    }

    nlohmann::json root;
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::error("Failed to open settings file for reading: {}", path.string());
            return;
        }
        file >> root;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse settings file '{}': {}", path.string(), e.what());
        return;
    }

    for (auto* feature : GetFeatureList()) {
        const auto& shortName = feature->GetShortName();
        if (!root.contains(shortName)) {
            spdlog::debug("No settings found for feature '{}', using defaults", feature->GetName());
            continue;
        }
        try {
            const auto& featureJson = root[shortName];
            if (featureJson.contains("enabled")) {
                feature->enabled = featureJson["enabled"].get<bool>();
            }
            feature->LoadSettings(featureJson);
            spdlog::info("Loaded settings for feature '{}'", feature->GetName());
        } catch (const std::exception& e) {
            spdlog::error("Failed to load settings for feature '{}': {}", feature->GetName(), e.what());
        }
    }

    spdlog::info("Settings loaded from {}", path.string());
}
