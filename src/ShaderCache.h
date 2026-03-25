#pragma once
#include "PCH.h"

class ShaderCache {
public:
    static ShaderCache& GetSingleton();

    void Initialize();

    struct CompiledShader {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
        std::vector<uint8_t> bytecode;
        bool valid = false;
    };

    // Compile a shader from HLSL file
    CompiledShader CompileShader(
        const std::wstring& hlslPath,
        const std::string& entryPoint,
        const std::string& target,
        const std::vector<D3D_SHADER_MACRO>& defines);

    // Build defines from technique ID bitfield for BSLightingShader
    // Ghidra-verified from BSLightingShader::BeginTechnique switch at 0x28B5C10:
    //   Bits 0-7: modifier flags (VC, Skinned, Precipitation, MultipleLayers, SoftLighting, RimLighting, CharacterLight, BackLighting)
    //   Bits 8-13: material type ((techID >> 8) & 0x3F):
    //     0=Default, 1=Envmap, 2=Glowmap, 3=Parallax, 4=Facegen, 5=FacegenRGBTint,
    //     6=Hair, 7=ParallaxOcc, 8=MTLand, 9=LODLand, 0xB=MultiLayerParallax,
    //     0xC=Tree, 0xD=LODObjects, 0xE=LODObjectsHD, 0x10=Eye,
    //     0x12=LODMultiTexLand, 0x13=Dismemberment
    //   Additional flags: 0x200=Shadows, 0x20000=SSS, 0x20000000=VRInstancedStereo
    std::vector<D3D_SHADER_MACRO> BuildDefines(uint32_t shaderType, uint32_t techniqueID, bool isPixelShader);

    // Add active feature defines
    void AddFeatureDefines(uint32_t shaderType, std::vector<D3D_SHADER_MACRO>& defines);

    // Disk cache
    bool LoadFromDiskCache(const std::string& key, CompiledShader& out);
    void SaveToDiskCache(const std::string& key, const CompiledShader& shader);
    std::string MakeCacheKey(uint32_t shaderType, uint32_t techniqueID, bool isPixel);

    bool diskCacheEnabled = true;
    std::filesystem::path diskCachePath{"Data/ShaderCache"};

    // Compilation statistics
    struct Stats {
        std::atomic<uint32_t> compiled{0};
        std::atomic<uint32_t> cacheHits{0};
        std::atomic<uint32_t> cacheMisses{0};
        std::atomic<uint32_t> errors{0};
    };
    Stats stats;

    // Get or compile a replacement pixel shader for a technique ID.
    // Returns nullptr if technique is unsupported or compilation fails (use vanilla).
    ID3D11PixelShader* GetOrCompilePS(uint32_t techniqueID);

    // Enable/disable technique-based replacement
    bool techniqueReplacementEnabled = false;

    // Clear disk cache and reset stats
    void Clear();

private:
    ShaderCache() = default;

    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D11PixelShader>> m_psCache;
    std::mutex m_psCacheMutex;

public:
    // Technique types our HLSL currently supports
    bool IsSupportedTechnique(uint32_t techniqueID) const;

private:
};
