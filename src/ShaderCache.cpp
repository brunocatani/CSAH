#include "ShaderCache.h"
#include "Feature.h"
#include "Globals.h"

ShaderCache& ShaderCache::GetSingleton() {
    static ShaderCache instance;
    return instance;
}

void ShaderCache::Initialize() {
    spdlog::info("ShaderCache: Initializing...");

    if (diskCacheEnabled) {
        try {
            std::filesystem::create_directories(diskCachePath);
            spdlog::info("ShaderCache: Disk cache directory ready at '{}'", diskCachePath.string());
        } catch (const std::exception& e) {
            spdlog::error("ShaderCache: Failed to create disk cache directory '{}': {}", diskCachePath.string(), e.what());
            diskCacheEnabled = false;
        }
    }

    spdlog::info("ShaderCache: Initialization complete (disk cache {})", diskCacheEnabled ? "enabled" : "disabled");
}

ShaderCache::CompiledShader ShaderCache::CompileShader(
    const std::wstring& hlslPath,
    const std::string& entryPoint,
    const std::string& target,
    const std::vector<D3D_SHADER_MACRO>& defines)
{
    CompiledShader result;

    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompileFromFile(
        hlslPath.c_str(),
        defines.empty() ? nullptr : defines.data(),
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        target.c_str(),
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr)) {
        std::string errorMsg = "Unknown compilation error";
        if (errorBlob) {
            errorMsg = std::string(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize());
        }

        // Convert wide path to narrow for logging
        auto narrowPath = std::filesystem::path(hlslPath).string();
        spdlog::error("ShaderCache: Failed to compile shader '{}' entry='{}' target='{}': {}",
            narrowPath, entryPoint, target, errorMsg);
        stats.errors++;
        return result;
    }

    // Store bytecode
    auto* data = static_cast<uint8_t*>(shaderBlob->GetBufferPointer());
    result.bytecode.assign(data, data + shaderBlob->GetBufferSize());

    // Create the appropriate shader object
    auto* device = Globals::GetDevice();
    if (!device) {
        spdlog::error("ShaderCache: D3D11 device is null, cannot create shader object");
        return result;
    }

    bool isVertex = target.starts_with("vs");
    bool isPixel = target.starts_with("ps");

    if (isVertex) {
        hr = device->CreateVertexShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr,
            &result.vs);
        if (FAILED(hr)) {
            spdlog::error("ShaderCache: CreateVertexShader failed (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            return result;
        }
    } else if (isPixel) {
        hr = device->CreatePixelShader(
            shaderBlob->GetBufferPointer(),
            shaderBlob->GetBufferSize(),
            nullptr,
            &result.ps);
        if (FAILED(hr)) {
            spdlog::error("ShaderCache: CreatePixelShader failed (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            return result;
        }
    } else {
        spdlog::error("ShaderCache: Unsupported shader target '{}'", target);
        return result;
    }

    result.valid = true;
    stats.compiled++;

    auto narrowPath = std::filesystem::path(hlslPath).string();
    spdlog::debug("ShaderCache: Compiled '{}' entry='{}' target='{}' ({} bytes)",
        narrowPath, entryPoint, target, result.bytecode.size());

    return result;
}

std::vector<D3D_SHADER_MACRO> ShaderCache::BuildDefines(uint32_t shaderType, uint32_t techniqueID, bool isPixelShader) {
    std::vector<D3D_SHADER_MACRO> defines;

    // Shader type identifier
    if (isPixelShader) {
        defines.push_back({"PSHADER", "1"});
    } else {
        defines.push_back({"VSHADER", "1"});
    }

    // Modifier flags (bits 0-7)
    if (techniqueID & 0x01) defines.push_back({"VC", "1"});
    if (techniqueID & 0x02) defines.push_back({"SKINNED", "1"});
    if (techniqueID & 0x04) defines.push_back({"HAS_PRECIPITATION", "1"});
    if (techniqueID & 0x08) defines.push_back({"MULTIPLE_LAYERS", "1"});
    if (techniqueID & 0x10) defines.push_back({"SOFT_LIGHTING", "1"});
    if (techniqueID & 0x20) defines.push_back({"RIM_LIGHTING", "1"});
    if (techniqueID & 0x40) defines.push_back({"CHARACTER_LIGHT", "1"});
    if (techniqueID & 0x80) defines.push_back({"BACK_LIGHTING", "1"});

    // Material type (bits 8-12)
    uint32_t materialType = (techniqueID >> 8) & 0x1F;
    switch (materialType) {
        case 0x00: break; // Default — no define needed
        case 0x01: defines.push_back({"MATERIAL_ENVMAP", "1"}); break;
        case 0x02: defines.push_back({"MATERIAL_GLOWMAP", "1"}); break;
        case 0x04: defines.push_back({"MATERIAL_PARALLAX", "1"}); break;
        case 0x05: defines.push_back({"MATERIAL_FACEGEN", "1"}); break;
        case 0x06: defines.push_back({"MATERIAL_FACEGEN_RGBTINT", "1"}); break;
        case 0x08: defines.push_back({"MATERIAL_HAIR", "1"}); break;
        case 0x0B: defines.push_back({"MATERIAL_EYE", "1"}); break;
        case 0x0C: defines.push_back({"MATERIAL_LODLANDSCAPE", "1"}); break;
        case 0x0D: defines.push_back({"MATERIAL_MULTITEX_LANDSCAPE", "1"}); break;
        case 0x0E: defines.push_back({"MATERIAL_LODOBJECTS", "1"}); break;
        case 0x10: defines.push_back({"MATERIAL_TREE", "1"}); break;
        case 0x12: defines.push_back({"MATERIAL_LOD_MULTITEX_LANDSCAPE", "1"}); break;
        case 0x13: defines.push_back({"MATERIAL_DISMEMBERMENT", "1"}); break;
        default:
            spdlog::warn("ShaderCache: Unknown material type 0x{:02X} in technique 0x{:08X}", materialType, techniqueID);
            break;
    }

    // Forward pass additional flags
    if (techniqueID & 0x0200) defines.push_back({"SHADOWS", "1"});
    if (techniqueID & 0x0800) defines.push_back({"PARALLAX_OCCLUSION_MAPPING", "1"});
    if (techniqueID & 0x20000) defines.push_back({"SUBSURFACE_SCATTERING", "1"});
    if (techniqueID & 0x20000000) defines.push_back({"VR_INSTANCED_STEREO", "1"});

    // Add feature defines from active features
    AddFeatureDefines(shaderType, defines);

    // Null terminator — required by D3DCompile
    defines.push_back({nullptr, nullptr});

    return defines;
}

void ShaderCache::AddFeatureDefines(uint32_t shaderType, std::vector<D3D_SHADER_MACRO>& defines) {
    for (auto* feature : Feature::GetFeatureList()) {
        if (!feature->enabled || !feature->loaded) {
            continue;
        }
        if (!feature->HasShaderDefine(shaderType)) {
            continue;
        }
        auto defineName = feature->GetShaderDefineName();
        if (defineName.empty()) {
            continue;
        }
        defines.push_back({defineName.data(), "1"});
        spdlog::trace("ShaderCache: Added feature define '{}' for shader type {}", defineName, shaderType);
    }
}

std::string ShaderCache::MakeCacheKey(uint32_t shaderType, uint32_t techniqueID, bool isPixel) {
    // Include feature state in key so cache invalidates when features change
    uint32_t featureHash = 0;
    for (auto* feature : Feature::GetFeatureList()) {
        if (feature->enabled && feature->loaded && feature->HasShaderDefine(shaderType)) {
            // Simple hash combining: rotate and xor with a hash of the feature name
            auto name = feature->GetShortName();
            uint32_t nameHash = 0;
            for (char c : name) {
                nameHash = nameHash * 31 + static_cast<uint32_t>(c);
            }
            featureHash ^= nameHash;
            featureHash = (featureHash << 7) | (featureHash >> 25);
        }
    }

    return std::format("shader_t{}_id{:08X}_{}_f{:08X}",
        shaderType, techniqueID, isPixel ? "ps" : "vs", featureHash);
}

bool ShaderCache::LoadFromDiskCache(const std::string& key, CompiledShader& out) {
    if (!diskCacheEnabled) {
        return false;
    }

    auto filePath = diskCachePath / (key + ".cso");
    if (!std::filesystem::exists(filePath)) {
        stats.cacheMisses++;
        return false;
    }

    try {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        auto size = file.tellg();
        if (size <= 0) {
            spdlog::warn("ShaderCache: Cached file '{}' is empty", filePath.string());
            return false;
        }

        file.seekg(0, std::ios::beg);
        out.bytecode.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(out.bytecode.data()), size);

        if (!file.good()) {
            spdlog::warn("ShaderCache: Failed to read cached file '{}'", filePath.string());
            out.bytecode.clear();
            return false;
        }

        // Create shader object from cached bytecode
        auto* device = Globals::GetDevice();
        if (!device) {
            spdlog::error("ShaderCache: D3D11 device is null, cannot create shader from cache");
            out.bytecode.clear();
            return false;
        }

        // Determine shader type from the key (contains "_ps_" or "_vs_")
        bool isPixel = key.find("_ps_") != std::string::npos;
        HRESULT hr;

        if (isPixel) {
            hr = device->CreatePixelShader(
                out.bytecode.data(), out.bytecode.size(), nullptr, &out.ps);
        } else {
            hr = device->CreateVertexShader(
                out.bytecode.data(), out.bytecode.size(), nullptr, &out.vs);
        }

        if (FAILED(hr)) {
            spdlog::warn("ShaderCache: Failed to create shader from cache '{}' (HRESULT: 0x{:08X})", key, static_cast<uint32_t>(hr));
            out.bytecode.clear();
            return false;
        }

        out.valid = true;
        stats.cacheHits++;
        spdlog::debug("ShaderCache: Loaded from disk cache '{}' ({} bytes)", key, out.bytecode.size());
        return true;

    } catch (const std::exception& e) {
        spdlog::warn("ShaderCache: Exception loading cache '{}': {}", key, e.what());
        return false;
    }
}

void ShaderCache::SaveToDiskCache(const std::string& key, const CompiledShader& shader) {
    if (!diskCacheEnabled) {
        return;
    }

    if (!shader.valid || shader.bytecode.empty()) {
        spdlog::warn("ShaderCache: Attempted to cache invalid shader '{}'", key);
        return;
    }

    try {
        std::filesystem::create_directories(diskCachePath);

        auto filePath = diskCachePath / (key + ".cso");
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("ShaderCache: Failed to open cache file for writing: {}", filePath.string());
            return;
        }

        file.write(reinterpret_cast<const char*>(shader.bytecode.data()),
            static_cast<std::streamsize>(shader.bytecode.size()));

        spdlog::debug("ShaderCache: Saved to disk cache '{}' ({} bytes)", key, shader.bytecode.size());

    } catch (const std::exception& e) {
        spdlog::error("ShaderCache: Failed to save cache '{}': {}", key, e.what());
    }
}

void ShaderCache::Clear() {
    if (!diskCacheEnabled) {
        spdlog::info("ShaderCache::Clear — disk cache is disabled, nothing to clear");
        return;
    }

    try {
        auto removed = std::filesystem::remove_all(diskCachePath);
        std::filesystem::create_directories(diskCachePath);
        spdlog::info("ShaderCache::Clear — removed {} cached entries", removed);
    } catch (const std::exception& e) {
        spdlog::error("ShaderCache::Clear — failed: {}", e.what());
    }

    stats.compiled = 0;
    stats.cacheHits = 0;
    stats.cacheMisses = 0;
    stats.errors = 0;
}
