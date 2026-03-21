#include "ShaderReplacer.h"
#include "ShaderCache.h"
#include "Globals.h"

ShaderReplacer& ShaderReplacer::GetSingleton()
{
    static ShaderReplacer instance;
    return instance;
}

void ShaderReplacer::WalkScatterTable(
    void* bsShader,
    size_t tableOffset,
    std::function<void(uint32_t techniqueID, void* shaderObj)> callback)
{
    if (!bsShader) {
        spdlog::error("ShaderReplacer::WalkScatterTable - null BSShader pointer");
        return;
    }

    auto tableBase = reinterpret_cast<uintptr_t>(bsShader) + tableOffset;

    // BSTScatterTable layout (FO4VR, from Ghidra BSShader::BeginTechnique):
    //   +0x04: bucketCount (uint32_t) — mask = count - 1
    //   +0x20: buckets pointer (ScatterEntry*)
    uint32_t bucketCount = *reinterpret_cast<uint32_t*>(tableBase + 0x04);
    uint32_t capacityMask = bucketCount > 0 ? bucketCount - 1 : 0;
    auto* buckets = *reinterpret_cast<ScatterEntry**>(tableBase + 0x20);

    if (!buckets) {
        spdlog::warn("ShaderReplacer::WalkScatterTable - null bucket array at BSShader+{:#x}", tableOffset);
        return;
    }

    if (capacityMask == 0) {
        spdlog::warn("ShaderReplacer::WalkScatterTable - capacityMask is 0 at BSShader+{:#x}", tableOffset);
        return;
    }

    // Sentinel pointer marks the end of chains
    auto* sentinel = *reinterpret_cast<ScatterEntry**>(tableBase + 0x10);

    spdlog::debug("ShaderReplacer::WalkScatterTable - tableOffset={:#x}, bucketCount={}, buckets={}, sentinel={}",
        tableOffset, capacityMask + 1, fmt::ptr(buckets), fmt::ptr(sentinel));

    uint32_t visited = 0;

    for (uint32_t i = 0; i <= capacityMask; ++i) {
        ScatterEntry* entry = &buckets[i];

        // An empty bucket has null data pointer or points to sentinel
        if (!entry->data) {
            continue;
        }

        // Walk the chain for this bucket
        for (ScatterEntry* cur = entry; cur != nullptr && cur != sentinel; cur = cur->next) {
            if (!cur->data) {
                continue;
            }

            // Key is at the start of the data struct
            uint32_t key = *reinterpret_cast<uint32_t*>(cur->data);
            callback(key, cur->data);
            ++visited;
        }
    }

    spdlog::debug("ShaderReplacer::WalkScatterTable - visited {} entries", visited);
}

bool ShaderReplacer::ReplacePixelShader(void* bsShader, uint32_t techniqueID, ID3D11PixelShader* customPS)
{
    if (!bsShader || !customPS) {
        spdlog::error("ShaderReplacer::ReplacePixelShader - null BSShader or customPS");
        return false;
    }

    bool replaced = false;

    WalkScatterTable(bsShader, kPSTableOffset, [&](uint32_t key, void* shaderObj) {
        if (key != techniqueID) {
            return;
        }

        // shaderObj is BSGraphics::PixelShader blob.
        // The ID3D11PixelShader* lives at blob + kD3DShaderPtrOffset.
        auto* slotAddr = reinterpret_cast<ID3D11PixelShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);

        ID3D11PixelShader* vanilla = *slotAddr;

        // Use technique ID as map key (no shaderType context in single-replace calls)
        uint64_t mapKey = static_cast<uint64_t>(techniqueID);

        // Save vanilla pointer only on first replacement
        if (vanillaPS.find(mapKey) == vanillaPS.end()) {
            vanillaPS[mapKey] = vanilla;
            spdlog::debug("ShaderReplacer: Saved vanilla PS for technique {:#010x} ({})",
                techniqueID, fmt::ptr(vanilla));
        }

        // Write the custom shader pointer
        *slotAddr = customPS;
        replaced = true;

        spdlog::debug("ShaderReplacer: Replaced PS for technique {:#010x}: {} -> {}",
            techniqueID, fmt::ptr(vanilla), fmt::ptr(customPS));
    });

    if (!replaced) {
        spdlog::warn("ShaderReplacer::ReplacePixelShader - technique {:#010x} not found in PS scatter table",
            techniqueID);
    }

    return replaced;
}

bool ShaderReplacer::ReplaceVertexShader(void* bsShader, uint32_t techniqueID, ID3D11VertexShader* customVS)
{
    if (!bsShader || !customVS) {
        spdlog::error("ShaderReplacer::ReplaceVertexShader - null BSShader or customVS");
        return false;
    }

    bool replaced = false;

    WalkScatterTable(bsShader, kVSTableOffset, [&](uint32_t key, void* shaderObj) {
        if (key != techniqueID) {
            return;
        }

        auto* slotAddr = reinterpret_cast<ID3D11VertexShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);

        ID3D11VertexShader* vanilla = *slotAddr;

        uint64_t mapKey = static_cast<uint64_t>(techniqueID);

        if (vanillaVS.find(mapKey) == vanillaVS.end()) {
            vanillaVS[mapKey] = vanilla;
            spdlog::debug("ShaderReplacer: Saved vanilla VS for technique {:#010x} ({})",
                techniqueID, fmt::ptr(vanilla));
        }

        *slotAddr = customVS;
        replaced = true;

        spdlog::debug("ShaderReplacer: Replaced VS for technique {:#010x}: {} -> {}",
            techniqueID, fmt::ptr(vanilla), fmt::ptr(customVS));
    });

    if (!replaced) {
        spdlog::warn("ShaderReplacer::ReplaceVertexShader - technique {:#010x} not found in VS scatter table",
            techniqueID);
    }

    return replaced;
}

void ShaderReplacer::ReplaceAllPermutations(void* bsShader, uint32_t shaderType)
{
    if (!bsShader) {
        spdlog::error("ShaderReplacer::ReplaceAllPermutations - null BSShader pointer");
        return;
    }

    auto& cache = ShaderCache::GetSingleton();

    // Map shader type to HLSL filename
    static const std::unordered_map<uint32_t, std::wstring> kShaderFiles = {
        {8, L"Data/Shaders/Community/Lighting.hlsl"},
        {6, L"Data/Shaders/Community/Grass.hlsl"},
    };
    auto fileIt = kShaderFiles.find(shaderType);
    if (fileIt == kShaderFiles.end()) {
        spdlog::warn("ShaderReplacer: No custom HLSL for shader type {}, skipping", shaderType);
        return;
    }
    const std::wstring& hlslPath = fileIt->second;

    // --- Pixel Shader pass ---
    uint32_t totalPS = 0;
    uint32_t replacedPS = 0;
    uint32_t failedPS = 0;
    uint32_t cachedPS = 0;

    spdlog::info("ShaderReplacer: Starting PS replacement pass for shader type {} at {}",
        shaderType, fmt::ptr(bsShader));

    WalkScatterTable(bsShader, kPSTableOffset, [&](uint32_t techniqueID, void* shaderObj) {
        ++totalPS;

        // Build a composite key: (shaderType << 32) | techniqueID
        uint64_t mapKey = (static_cast<uint64_t>(shaderType) << 32) | techniqueID;

        // Save vanilla pointer before any replacement
        auto* slotAddr = reinterpret_cast<ID3D11PixelShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);
        ID3D11PixelShader* vanilla = *slotAddr;

        if (vanillaPS.find(mapKey) == vanillaPS.end()) {
            vanillaPS[mapKey] = vanilla;
        }

        // Try disk cache first
        std::string cacheKey = cache.MakeCacheKey(shaderType, techniqueID, true);
        ShaderCache::CompiledShader compiled;

        if (cache.LoadFromDiskCache(cacheKey, compiled) && compiled.valid && compiled.ps) {
            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;
            ++cachedPS;
            spdlog::trace("ShaderReplacer: PS {:#010x} loaded from disk cache", techniqueID);
            return;
        }

        // Build defines and compile from HLSL source
        auto defines = cache.BuildDefines(shaderType, techniqueID, true);

        compiled = cache.CompileShader(hlslPath, "PSMain", "ps_5_0", defines);

        if (compiled.valid && compiled.ps) {
            cache.SaveToDiskCache(cacheKey, compiled);

            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;

            spdlog::trace("ShaderReplacer: PS {:#010x} compiled and replaced", techniqueID);
        } else {
            ++failedPS;
            spdlog::trace("ShaderReplacer: PS {:#010x} compile failed, keeping vanilla", techniqueID);
        }
    });

    spdlog::info("ShaderReplacer: PS pass complete - total={}, replaced={} (cached={}), failed={}, vanilla-kept={}",
        totalPS, replacedPS, cachedPS, failedPS, totalPS - replacedPS);

    // --- Vertex Shader pass ---
    uint32_t totalVS = 0;
    uint32_t replacedVS = 0;
    uint32_t failedVS = 0;
    uint32_t cachedVS = 0;

    spdlog::info("ShaderReplacer: Starting VS replacement pass for shader type {} at {}",
        shaderType, fmt::ptr(bsShader));

    WalkScatterTable(bsShader, kVSTableOffset, [&](uint32_t techniqueID, void* shaderObj) {
        ++totalVS;

        uint64_t mapKey = (static_cast<uint64_t>(shaderType) << 32) | techniqueID;

        auto* slotAddr = reinterpret_cast<ID3D11VertexShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);
        ID3D11VertexShader* vanilla = *slotAddr;

        if (vanillaVS.find(mapKey) == vanillaVS.end()) {
            vanillaVS[mapKey] = vanilla;
        }

        // Try disk cache first
        std::string cacheKey = cache.MakeCacheKey(shaderType, techniqueID, false);
        ShaderCache::CompiledShader compiled;

        if (cache.LoadFromDiskCache(cacheKey, compiled) && compiled.valid && compiled.vs) {
            *slotAddr = compiled.vs.Get();
            keepAliveVS.push_back(std::move(compiled.vs));
            ++replacedVS;
            ++cachedVS;
            spdlog::trace("ShaderReplacer: VS {:#010x} loaded from disk cache", techniqueID);
            return;
        }

        auto defines = cache.BuildDefines(shaderType, techniqueID, false);

        compiled = cache.CompileShader(hlslPath, "VSMain", "vs_5_0", defines);

        if (compiled.valid && compiled.vs) {
            cache.SaveToDiskCache(cacheKey, compiled);

            *slotAddr = compiled.vs.Get();
            keepAliveVS.push_back(std::move(compiled.vs));
            ++replacedVS;

            spdlog::trace("ShaderReplacer: VS {:#010x} compiled and replaced", techniqueID);
        } else {
            ++failedVS;
            spdlog::trace("ShaderReplacer: VS {:#010x} compile failed, keeping vanilla", techniqueID);
        }
    });

    spdlog::info("ShaderReplacer: VS pass complete - total={}, replaced={} (cached={}), failed={}, vanilla-kept={}",
        totalVS, replacedVS, cachedVS, failedVS, totalVS - replacedVS);

    spdlog::info("ShaderReplacer: Replacement complete for shader type {} - PS {}/{}, VS {}/{}",
        shaderType, replacedPS, totalPS, replacedVS, totalVS);
}

void ShaderReplacer::ReplaceFilteredPermutations(void* bsShader, uint32_t shaderType,
    std::function<bool(uint32_t techniqueID)> filter)
{
    if (!bsShader) {
        spdlog::error("ShaderReplacer::ReplaceFilteredPermutations - null BSShader pointer");
        return;
    }

    auto& cache = ShaderCache::GetSingleton();

    // Map shader type to HLSL filename
    static const std::unordered_map<uint32_t, std::wstring> kShaderFiles = {
        {8, L"Data/Shaders/Community/Lighting.hlsl"},
        {6, L"Data/Shaders/Community/Grass.hlsl"},
    };
    auto fileIt = kShaderFiles.find(shaderType);
    if (fileIt == kShaderFiles.end()) {
        spdlog::warn("ShaderReplacer: No custom HLSL for shader type {}, skipping", shaderType);
        return;
    }
    const std::wstring& hlslPath = fileIt->second;

    // --- Pixel Shader pass only (VS stays vanilla) ---
    uint32_t totalPS = 0;
    uint32_t replacedPS = 0;
    uint32_t failedPS = 0;
    uint32_t cachedPS = 0;
    uint32_t skippedPS = 0;

    spdlog::info("ShaderReplacer: Starting filtered PS replacement pass for shader type {} at {}",
        shaderType, fmt::ptr(bsShader));

    WalkScatterTable(bsShader, kPSTableOffset, [&](uint32_t techniqueID, void* shaderObj) {
        ++totalPS;

        // Apply filter — skip permutations that don't match
        if (!filter(techniqueID)) {
            ++skippedPS;
            return;
        }

        // Build a composite key: (shaderType << 32) | techniqueID
        uint64_t mapKey = (static_cast<uint64_t>(shaderType) << 32) | techniqueID;

        // Resolve the D3D pointer slot address
        auto* slotAddr = reinterpret_cast<ID3D11PixelShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);
        ID3D11PixelShader* vanilla = *slotAddr;

        // Save vanilla pointer and slot address only on first replacement
        if (vanillaPS.find(mapKey) == vanillaPS.end()) {
            vanillaPS[mapKey] = vanilla;
            vanillaPSSlots[mapKey] = slotAddr;
        }

        // Try disk cache first
        std::string cacheKey = cache.MakeCacheKey(shaderType, techniqueID, true);
        ShaderCache::CompiledShader compiled;

        if (cache.LoadFromDiskCache(cacheKey, compiled) && compiled.valid && compiled.ps) {
            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;
            ++cachedPS;
            spdlog::trace("ShaderReplacer: Filtered PS {:#010x} loaded from disk cache", techniqueID);
            return;
        }

        // Build defines and compile from HLSL source
        auto defines = cache.BuildDefines(shaderType, techniqueID, true);

        compiled = cache.CompileShader(hlslPath, "PSMain", "ps_5_0", defines);

        if (compiled.valid && compiled.ps) {
            cache.SaveToDiskCache(cacheKey, compiled);

            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;

            spdlog::trace("ShaderReplacer: Filtered PS {:#010x} compiled and replaced", techniqueID);
        } else {
            ++failedPS;
            spdlog::trace("ShaderReplacer: Filtered PS {:#010x} compile failed, keeping vanilla", techniqueID);
        }
    });

    spdlog::info("ShaderReplacer: Filtered PS pass complete - total={}, skipped={}, replaced={} (cached={}), failed={}, vanilla-kept={}",
        totalPS, skippedPS, replacedPS, cachedPS, failedPS, totalPS - skippedPS - replacedPS);
}

void ShaderReplacer::RestoreAllVanillaPS()
{
    uint32_t restored = 0;
    uint32_t missing = 0;

    for (auto& [mapKey, vanilla] : vanillaPS) {
        auto slotIt = vanillaPSSlots.find(mapKey);
        if (slotIt == vanillaPSSlots.end() || !slotIt->second) {
            ++missing;
            spdlog::warn("ShaderReplacer::RestoreAllVanillaPS - no slot address for key {:#018x}", mapKey);
            continue;
        }

        *slotIt->second = vanilla;
        ++restored;

        spdlog::trace("ShaderReplacer::RestoreAllVanillaPS - restored key {:#018x} -> {}",
            mapKey, fmt::ptr(vanilla));
    }

    spdlog::info("ShaderReplacer::RestoreAllVanillaPS - restored={}, missing-slots={}", restored, missing);
}
