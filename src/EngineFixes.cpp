#include "PCH.h"
#include "EngineFixes.h"

namespace EngineFixes
{
    // =========================================================================
    // Module base helper
    // =========================================================================
    static std::uintptr_t GetBase()
    {
        return REL::Module::get().base();
    }

    // =========================================================================
    // PatchByte — validates expected byte, applies replacement, flushes icache
    // =========================================================================
    static bool PatchByte(std::uintptr_t addr, std::uint8_t expected, std::uint8_t replacement,
                          const char* label)
    {
        auto* p = reinterpret_cast<std::uint8_t*>(addr);

        if (*p == replacement) {
            spdlog::info("[EngineFixes]   {} already patched (0x{:02X})", label, replacement);
            return true;
        }
        if (*p != expected) {
            spdlog::warn("[EngineFixes]   {} unexpected byte at 0x{:X}: 0x{:02X} (expected 0x{:02X})",
                         label, addr, *p, expected);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            spdlog::error("[EngineFixes]   {} VirtualProtect failed (err {})", label, GetLastError());
            return false;
        }

        *p = replacement;
        VirtualProtect(p, 1, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), p, 1);

        spdlog::info("[EngineFixes]   {} patched: 0x{:02X} -> 0x{:02X}", label, expected, replacement);
        return true;
    }

    // =========================================================================
    // PatchBytes — validates N expected bytes, writes N copies of a single value
    // =========================================================================
    static bool PatchBytes(std::uintptr_t addr, const std::uint8_t* expected, std::size_t count,
                           std::uint8_t replacement, const char* label)
    {
        auto* p = reinterpret_cast<std::uint8_t*>(addr);

        // Check already patched
        bool alreadyPatched = true;
        for (std::size_t i = 0; i < count; i++) {
            if (p[i] != replacement) { alreadyPatched = false; break; }
        }
        if (alreadyPatched) {
            spdlog::info("[EngineFixes]   {} already patched ({} bytes)", label, count);
            return true;
        }

        // Verify expected
        for (std::size_t i = 0; i < count; i++) {
            if (p[i] != expected[i]) {
                spdlog::warn("[EngineFixes]   {} byte mismatch at +{}: 0x{:02X} (expected 0x{:02X})",
                             label, i, p[i], expected[i]);
                return false;
            }
        }

        DWORD oldProtect;
        if (!VirtualProtect(p, count, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            spdlog::error("[EngineFixes]   {} VirtualProtect failed", label);
            return false;
        }

        for (std::size_t i = 0; i < count; i++) {
            p[i] = replacement;
        }

        VirtualProtect(p, count, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), p, count);

        spdlog::info("[EngineFixes]   {} patched: {} bytes -> NOP", label, count);
        return true;
    }

    // =========================================================================
    // PatchRawBytes — writes arbitrary byte sequence (with expected-byte validation)
    // =========================================================================
    static bool PatchRawBytes(std::uintptr_t addr, const std::uint8_t* expected,
                              const std::uint8_t* replacement, std::size_t count,
                              const char* label)
    {
        auto* p = reinterpret_cast<std::uint8_t*>(addr);

        // Check already patched
        bool alreadyPatched = true;
        for (std::size_t i = 0; i < count; i++) {
            if (p[i] != replacement[i]) { alreadyPatched = false; break; }
        }
        if (alreadyPatched) {
            spdlog::info("[EngineFixes]   {} already patched ({} bytes)", label, count);
            return true;
        }

        // Verify expected
        for (std::size_t i = 0; i < count; i++) {
            if (p[i] != expected[i]) {
                spdlog::warn("[EngineFixes]   {} byte mismatch at +{}: 0x{:02X} (expected 0x{:02X})",
                             label, i, p[i], expected[i]);
                return false;
            }
        }

        DWORD oldProtect;
        if (!VirtualProtect(p, count, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            spdlog::error("[EngineFixes]   {} VirtualProtect failed", label);
            return false;
        }

        std::memcpy(p, replacement, count);
        VirtualProtect(p, count, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), p, count);

        spdlog::info("[EngineFixes]   {} patched: {} bytes", label, count);
        return true;
    }

    // =========================================================================
    // PatchMovRipToImm — replaces MOV reg, [RIP+disp32] with MOV reg, imm32
    // Adapted from VR-Shadow-Boost cascade_patch.cpp
    // =========================================================================
    static bool PatchMovRipToImm(std::uintptr_t instrRVA, std::uintptr_t globalRVA,
                                 std::uint32_t newValue, const char* label)
    {
        auto base = GetBase();
        auto* ip = reinterpret_cast<std::uint8_t*>(base + instrRVA);

        // Detect optional REX prefix (0x40-0x4F)
        bool hasRex = false;
        std::uint8_t rexByte = 0;
        int opcodeIdx = 0;

        if ((ip[0] & 0xF0) == 0x40) {
            hasRex = true;
            rexByte = ip[0];
            opcodeIdx = 1;
        }

        // Verify opcode is 0x8B (MOV r32, r/m32)
        if (ip[opcodeIdx] != 0x8B) {
            spdlog::warn("[EngineFixes]   {} opcode 0x{:02X} != 0x8B", label, ip[opcodeIdx]);
            return false;
        }

        // Verify ModRM: mod=00, rm=101 (RIP-relative)
        std::uint8_t modrm = ip[opcodeIdx + 1];
        if ((modrm & 0xC7) != 0x05) {
            spdlog::warn("[EngineFixes]   {} ModRM 0x{:02X} not RIP-relative", label, modrm);
            return false;
        }

        // Calculate expected displacement
        int instrLen = opcodeIdx + 2 + 4;  // [REX] + opcode + ModRM + disp32
        std::uint32_t expectedDisp = static_cast<std::uint32_t>(globalRVA - (instrRVA + instrLen));
        std::uint32_t actualDisp = *reinterpret_cast<std::uint32_t*>(ip + opcodeIdx + 2);

        if (actualDisp != expectedDisp) {
            spdlog::warn("[EngineFixes]   {} disp 0x{:08X} != expected 0x{:08X}",
                         label, actualDisp, expectedDisp);
            return false;
        }

        // Extract destination register
        std::uint8_t reg = (modrm >> 3) & 7;
        bool extReg = hasRex && (rexByte & 0x04);  // REX.R extends reg field

        // Build replacement: MOV reg, imm32
        std::uint8_t newInstr[8];
        int newLen;
        if (extReg) {
            newInstr[0] = 0x41;          // REX.B (for extended registers)
            newInstr[1] = 0xB8 + reg;    // MOV r32, imm32
            std::memcpy(newInstr + 2, &newValue, 4);
            newLen = 6;
        } else {
            newInstr[0] = 0xB8 + reg;    // MOV r32, imm32
            std::memcpy(newInstr + 1, &newValue, 4);
            newLen = 5;
        }

        // NOP-pad remaining bytes
        for (int i = newLen; i < instrLen; i++) {
            newInstr[i] = 0x90;
        }

        // Apply patch
        DWORD oldProtect;
        if (!VirtualProtect(ip, instrLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            spdlog::error("[EngineFixes]   {} VirtualProtect failed", label);
            return false;
        }

        std::memcpy(ip, newInstr, instrLen);
        VirtualProtect(ip, instrLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), ip, instrLen);

        spdlog::info("[EngineFixes]   {} patched: MOV reg, [RIP+disp] -> MOV reg, {} ({} bytes)",
                     label, newValue, instrLen);
        return true;
    }

    // =========================================================================
    // AllocateNearby — allocate executable memory within +/-2GB of target
    // Required for JMP rel32 code caves
    // =========================================================================
    static void* AllocateNearby(std::uintptr_t target, std::size_t size)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        std::uintptr_t granularity = si.dwAllocationGranularity;

        for (std::uintptr_t offset = granularity; offset < 0x7F000000; offset += granularity) {
            // Try above
            void* p = VirtualAlloc(reinterpret_cast<void*>(target + offset),
                                   size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return p;

            // Try below
            if (target > offset) {
                p = VirtualAlloc(reinterpret_cast<void*>(target - offset),
                                 size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (p) return p;
            }
        }
        return nullptr;
    }

    // =========================================================================
    // Code cave state — kept alive for process lifetime
    // =========================================================================
    static void* g_nullSafetyCave       = nullptr;
    static void* g_nodeAllocCave        = nullptr;
    static void* g_entryZeroInitCave    = nullptr;
    static void* g_ptrValidationCave    = nullptr;

    // =========================================================================
    // Tiled Lighting Offsets (from F4VR-Tiled-Lighting TiledLighting.h)
    // =========================================================================
    namespace TiledLightingOffsets
    {
        constexpr std::uint8_t NOP = 0x90;

        // InitSDM: tile dimension gate — JZ +0x2B (short, 2 bytes)
        constexpr std::uintptr_t InitSDM_JZ1       = 0x2889acf;
        constexpr std::uint8_t   InitSDM_JZ1_Old1  = 0x74;
        constexpr std::uint8_t   InitSDM_JZ1_Old2  = 0x2B;

        // InitSDM: structured buffer creation gate — JZ near (6 bytes)
        constexpr std::uintptr_t InitSDM_JZ2       = 0x2889b62;
        constexpr std::uint8_t   InitSDM_JZ2_Old[6] = { 0x0F, 0x84, 0xFA, 0x00, 0x00, 0x00 };

        // Render targets 0x6c/0x6d — JZ +0x3A (short, 2 bytes)
        constexpr std::uintptr_t RT_JZ1            = 0x28a5a86;
        constexpr std::uint8_t   RT_JZ1_Old1       = 0x74;
        constexpr std::uint8_t   RT_JZ1_Old2       = 0x3A;

        // Render targets 0x26/0x27 — JZ +0x3A (short, 2 bytes)
        constexpr std::uintptr_t RT_JZ2            = 0x28a5bcc;
        constexpr std::uint8_t   RT_JZ2_Old1       = 0x74;
        constexpr std::uint8_t   RT_JZ2_Old2       = 0x3A;

        // Render dispatch VR mode skip — JNZ +0x16 (short, 2 bytes)
        constexpr std::uintptr_t Dispatch_JNZ      = 0x27ee5cc;
        constexpr std::uint8_t   Dispatch_JNZ_Old1 = 0x75;
        constexpr std::uint8_t   Dispatch_JNZ_Old2 = 0x16;
    }

    // =========================================================================
    // Shadow Cascade Offsets (from VR-Shadow-Boost cascade_patch.h)
    // =========================================================================
    namespace CascadeOffsets
    {
        // Cascade count global
        constexpr std::uintptr_t CountGlobal    = 0x3924818;
        constexpr std::uint32_t  DesiredCount   = 4;

        // MOV read sites (patch RIP-relative reads to immediate 4)
        constexpr std::uintptr_t CtorRead       = 0x27e929a;   // FUN_1427e8f50
        constexpr std::uintptr_t SetupRead      = 0x290dc03;   // FUN_14290dbd0
        constexpr std::uintptr_t RenderRead1    = 0x28a57a0;   // FUN_1428a4a60
        constexpr std::uintptr_t RenderRead2    = 0x28a5c3c;   // FUN_1428a4a60

        // CMP immediate at setup site: 2 -> 4
        constexpr std::uintptr_t SetupCmpImm    = 0x290dc09;
        constexpr std::uint8_t   SetupCmpOld    = 0x02;
        constexpr std::uint8_t   SetupCmpNew    = 0x04;

        // Shader constructor patches (2 -> 4)
        constexpr std::uintptr_t ArrayCapByte   = 0x27c340c;
        constexpr std::uint8_t   ArrayCapOld    = 0x02;
        constexpr std::uint8_t   ArrayCapNew    = 0x04;

        constexpr std::uintptr_t StoredCountByte = 0x27c34d8;
        constexpr std::uint8_t   StoredCountOld  = 0x02;
        constexpr std::uint8_t   StoredCountNew  = 0x04;

        // Shadow distance (4-cascade, .rdata)
        constexpr std::uintptr_t ShadowDist4Cascade = 0x2c7f648;
        constexpr std::uintptr_t ShadowDist2Cascade = 0x3924808;

        // Mask writer safe mode patches
        constexpr std::uintptr_t InitMask_Byte     = 0x284e9fb;
        constexpr std::uint8_t   InitMask_Old      = 0x0F;
        constexpr std::uint8_t   InitMask_Safe     = 0x03;

        constexpr std::uintptr_t FallbackMask_Byte = 0x284ea38;
        constexpr std::uint8_t   FallbackMask_Old  = 0x0F;
        constexpr std::uint8_t   FallbackMask_Safe = 0x03;

        constexpr std::uintptr_t ArrayEntry1_Byte  = 0x284ea4c;
        constexpr std::uint8_t   ArrayEntry1_Old   = 0x05;
        constexpr std::uint8_t   ArrayEntry1_Safe  = 0x03;

        constexpr std::uintptr_t ArrayEntry3_Byte  = 0x284ea5f;
        constexpr std::uint8_t   ArrayEntry3_Old   = 0x09;
        constexpr std::uint8_t   ArrayEntry3_Safe  = 0x03;

        // Shared shadow maps: RIGHT eye uses LEFT eye's shadow maps
        constexpr std::uintptr_t SharedShadow_Activate = 0x290d9d0;  // MOV RCX,[R15+0x58] disp byte
        constexpr std::uintptr_t SharedShadow_Dispatch = 0x290d9d9;  // MOV RDX,[R15+0x58] disp byte
        constexpr std::uint8_t   SharedShadow_Old      = 0x58;
        constexpr std::uint8_t   SharedShadow_New      = 0x50;
    }

    // =========================================================================
    // Stereo Dispatch Offsets
    // =========================================================================
    namespace StereoOffsets
    {
        constexpr std::uintptr_t JzInstrRVA = 0x281be1c;
        constexpr std::uint8_t   JzOpcode   = 0x74;
        constexpr std::uint8_t   JmpOpcode  = 0xEB;
    }

    // =========================================================================
    // Crash Prevention Offsets
    // =========================================================================
    namespace CrashOffsets
    {
        // Null safety: FUN_142813740 — mov rbp, [r10+0x180] (7 bytes)
        constexpr std::uintptr_t NullSafety_RVA   = 0x281377F;
        constexpr std::size_t    NullSafety_Size   = 7;
        constexpr std::uint8_t   NullSafety_Expected[7] = { 0x49, 0x8B, 0xAA, 0x80, 0x01, 0x00, 0x00 };

        // Node allocator: FUN_14278e610 — prologue: sub rsp,0x68; mov r10,r9 (7 bytes)
        constexpr std::uintptr_t NodeAlloc_RVA     = 0x278e610;
        constexpr std::size_t    NodeAlloc_Size    = 7;
        constexpr std::uint8_t   NodeAlloc_Expected[7] = { 0x48, 0x83, 0xEC, 0x68, 0x4D, 0x8B, 0xD1 };

        // Cascade entry zero-init: FUN_1427a51e0 — mov [rax+r10+0x90], rdx (8 bytes)
        constexpr std::uintptr_t ZeroInit_RVA      = 0x27A52A0;
        constexpr std::size_t    ZeroInit_Size     = 8;
        constexpr std::uintptr_t ZeroInit_ReturnRVA = 0x27A52A8;
        constexpr std::uint8_t   ZeroInit_Expected[8] = { 0x4A, 0x89, 0x94, 0x10, 0x90, 0x00, 0x00, 0x00 };

        // Cascade pointer validation: FUN_1427a3f90+0xA4A
        // test r14,r14 (3) + jz near (6) = 9 bytes
        constexpr std::uintptr_t PtrValid_RVA       = 0x27A49DA;
        constexpr std::size_t    PtrValid_Size      = 9;
        constexpr std::uintptr_t PtrValid_SkipTarget = 0x27A4A6D;
        constexpr std::uintptr_t PtrValid_Continue   = 0x27A49E3;
        constexpr std::uint8_t   PtrValid_Expected[9] = { 0x4D, 0x85, 0xF6, 0x0F, 0x84, 0x8A, 0x00, 0x00, 0x00 };
    }

    // =========================================================================
    // CascadeRuntime — timer-based VR array expansion and mask restoration
    // Ported from VR-Shadow-Boost cascade_patch.cpp runtime logic
    // =========================================================================
    namespace CascadeRuntime
    {
        // ----- Constants (from VR-Shadow-Boost cascade_patch.cpp) -----

        // VR cascade array container
        constexpr std::uintptr_t ArrayPtr       = 0x6878b18;   // pBuf at +0x00, capacity at +0x08
        constexpr std::uintptr_t ArrayCount     = 0x6878b28;   // count (uint32)
        constexpr std::size_t    EntrySize       = 0x180;
        constexpr std::uint32_t  TargetCount     = 4;

        // Pool self-ref offsets within each VR array entry
        constexpr std::uintptr_t PoolOffsets[]   = { 0x70, 0xA8, 0xE8, 0x128 };

        // Scene node pointers
        constexpr std::uintptr_t SceneNodePtr    = 0x6879520;  // Render scene node
        constexpr std::uintptr_t SceneNodePtr2   = 0x6885d40;  // Setup scene node

        // Cascade group offsets (relative to scene node)
        constexpr std::uintptr_t CascadeGroupOff = 0x248;
        constexpr std::uintptr_t VRFlagOff       = 0x173;      // uint8, must be 1
        constexpr std::uintptr_t FlatCountOff    = 0x190;      // uint32
        constexpr std::uintptr_t FlatBufferOff   = 0x198;      // uintptr_t
        constexpr std::uintptr_t ShaderObjOff    = 0x2B8;      // uintptr_t

        // Flat entry layout
        constexpr std::size_t    FlatEntrySize      = 0x110;
        constexpr std::uintptr_t FlatShadowMapOff   = 0x50;
        constexpr std::uintptr_t FlatLastCascadeOff = 0x102;

        // Shader object fields
        constexpr std::uintptr_t ShaderStoredCountOff = 0x1D8;  // uint32
        constexpr std::uintptr_t ShaderArrayCapOff    = 0x168;  // uint16
        constexpr std::uintptr_t ShaderArrayCntOff    = 0x16A;  // uint16

        // Mask rotation global
        constexpr std::uintptr_t MaskGlobal      = 0x6885cc4;

        // Cascade count global (same as CascadeOffsets::CountGlobal)
        constexpr std::uintptr_t CountGlobal     = 0x3924818;

        // ----- State tracking -----
        static HANDLE   g_timerHandle  = nullptr;
        static bool     g_vrExpanded   = false;
        static bool     g_maskRestored = false;
        static uint32_t g_tickCount    = 0;

        // ----- ForceCascadeCount4 -----
        static void ForceCascadeCount4()
        {
            auto base = GetBase();
            auto* p = reinterpret_cast<volatile std::uint32_t*>(base + CountGlobal);
            if (*p != 4) {
                *p = 4;
                spdlog::trace("[CascadeRuntime] Forced cascade count global to 4");
            }
        }

        // ----- FixSetupSceneNode -----
        // If setup scene node is null, copy render scene node pointer there
        static void FixSetupSceneNode()
        {
            auto base = GetBase();
            auto renderNode = *reinterpret_cast<std::uintptr_t*>(base + SceneNodePtr);
            auto* pSetup    = reinterpret_cast<std::uintptr_t*>(base + SceneNodePtr2);

            if (renderNode != 0 && *pSetup == 0) {
                *pSetup = renderNode;
                spdlog::info("[CascadeRuntime] Copied render scene node 0x{:X} to setup slot", renderNode);
            }
        }

        // ----- ForceCascadeGroupVRFlag -----
        // For both scene nodes, force VR flag byte at cascade_group+0x173 to 1
        static void ForceCascadeGroupVRFlag()
        {
            auto base = GetBase();
            std::uintptr_t nodeAddrs[] = { base + SceneNodePtr, base + SceneNodePtr2 };

            for (auto nodeAddr : nodeAddrs) {
                auto node = *reinterpret_cast<std::uintptr_t*>(nodeAddr);
                if (node == 0) continue;

                auto cg = *reinterpret_cast<std::uintptr_t*>(node + CascadeGroupOff);
                if (cg == 0) continue;

                auto* vrFlag = reinterpret_cast<std::uint8_t*>(cg + VRFlagOff);
                if (*vrFlag == 0) {
                    *vrFlag = 1;
                    spdlog::info("[CascadeRuntime] Forced VR flag at CG 0x{:X}+0x173 to 1", cg);
                }
            }
        }

        // ----- ForceShaderFields -----
        // Force shader stored count, array cap, and array cnt to >= 4
        static void ForceShaderFields()
        {
            auto base = GetBase();
            std::uintptr_t nodeAddrs[] = { base + SceneNodePtr, base + SceneNodePtr2 };

            for (auto nodeAddr : nodeAddrs) {
                auto node = *reinterpret_cast<std::uintptr_t*>(nodeAddr);
                if (node == 0) continue;

                auto cg = *reinterpret_cast<std::uintptr_t*>(node + CascadeGroupOff);
                if (cg == 0) continue;

                auto shader = *reinterpret_cast<std::uintptr_t*>(cg + ShaderObjOff);
                if (shader == 0) continue;

                auto* storedCount = reinterpret_cast<std::uint32_t*>(shader + ShaderStoredCountOff);
                auto* arrayCap    = reinterpret_cast<std::uint16_t*>(shader + ShaderArrayCapOff);
                auto* arrayCnt    = reinterpret_cast<std::uint16_t*>(shader + ShaderArrayCntOff);

                if (*storedCount < 4) {
                    spdlog::info("[CascadeRuntime] Shader 0x{:X}: stored count {} -> 4", shader, *storedCount);
                    *storedCount = 4;
                }
                if (*arrayCap < 4) {
                    spdlog::info("[CascadeRuntime] Shader 0x{:X}: array cap {} -> 4", shader, *arrayCap);
                    *arrayCap = 4;
                }
                if (*arrayCnt < 4) {
                    spdlog::info("[CascadeRuntime] Shader 0x{:X}: array cnt {} -> 4", shader, *arrayCnt);
                    *arrayCnt = 4;
                }
            }
        }

        // ----- TryExpandVRArray -----
        // Expand VR cascade array from 2 entries to 4 by template-copying entry 0
        static void TryExpandVRArray()
        {
            if (g_vrExpanded) return;

            auto base = GetBase();

            // Container: pBuf at ArrayPtr+0x00, capacity at ArrayPtr+0x08, count at ArrayCount
            auto* pBuf      = reinterpret_cast<std::uintptr_t*>(base + ArrayPtr);
            auto* pCapacity = reinterpret_cast<std::uint64_t*>(base + ArrayPtr + 0x08);
            auto* pCount    = reinterpret_cast<std::uint32_t*>(base + ArrayCount);

            std::uintptr_t buf = *pBuf;
            std::uint64_t  cap = *pCapacity;
            std::uint32_t  cnt = *pCount;

            if (cnt >= TargetCount) {
                spdlog::info("[CascadeRuntime] VR array already has {} entries, marking expanded", cnt);
                g_vrExpanded = true;
                return;
            }

            if (buf == 0) {
                spdlog::trace("[CascadeRuntime] VR array buffer is null, waiting...");
                return;
            }

            // Template: entry 0
            auto* templateEntry = reinterpret_cast<std::uint8_t*>(buf);

            auto fixupEntry = [](std::uint8_t* dst) {
                // Clear spinlock at entry start
                *reinterpret_cast<std::uint32_t*>(dst + 0x00) = 0;
                *reinterpret_cast<std::uint32_t*>(dst + 0x04) = 0;

                // Reset pool self-ref pointers
                for (auto poolOff : PoolOffsets) {
                    *reinterpret_cast<std::uintptr_t*>(dst + poolOff) = 0;
                    *reinterpret_cast<std::uintptr_t*>(dst + poolOff + 8) =
                        reinterpret_cast<std::uintptr_t>(dst + poolOff);
                }
            };

            if (cap >= TargetCount) {
                // In-place expansion: buffer has enough capacity
                spdlog::info("[CascadeRuntime] In-place VR array expansion (cap={}, cnt={}->4)", cap, cnt);

                for (std::uint32_t i = cnt; i < TargetCount; i++) {
                    auto* dst = reinterpret_cast<std::uint8_t*>(buf + i * EntrySize);
                    std::memcpy(dst, templateEntry, EntrySize);
                    fixupEntry(dst);
                }

                // Fix existing entries' pool tails if zero
                for (std::uint32_t i = 0; i < cnt; i++) {
                    auto* entry = reinterpret_cast<std::uint8_t*>(buf + i * EntrySize);
                    for (auto poolOff : PoolOffsets) {
                        auto* pTail = reinterpret_cast<std::uintptr_t*>(entry + poolOff + 8);
                        if (*pTail == 0) {
                            *pTail = reinterpret_cast<std::uintptr_t>(entry + poolOff);
                        }
                    }
                }

                *pCount = TargetCount;
                g_vrExpanded = true;
                spdlog::info("[CascadeRuntime] VR array expanded in-place to {} entries", TargetCount);

            } else {
                // Need to allocate new buffer
                spdlog::info("[CascadeRuntime] Allocating new VR array buffer (cap={} < 4)", cap);

                std::size_t newSize = TargetCount * EntrySize;
                void* newBuf = VirtualAlloc(nullptr, newSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!newBuf) {
                    spdlog::error("[CascadeRuntime] VirtualAlloc failed for VR array (size={})", newSize);
                    return;
                }

                std::memset(newBuf, 0, newSize);

                // Copy existing entries
                std::memcpy(newBuf, reinterpret_cast<void*>(buf), cnt * EntrySize);

                // Template-copy new entries from entry 0
                auto* newBase = reinterpret_cast<std::uint8_t*>(newBuf);
                for (std::uint32_t i = cnt; i < TargetCount; i++) {
                    auto* dst = newBase + i * EntrySize;
                    std::memcpy(dst, newBase, EntrySize);  // copy from entry 0 of new buffer
                    fixupEntry(dst);
                }

                // Fix all entries' pool tails
                for (std::uint32_t i = 0; i < TargetCount; i++) {
                    auto* entry = newBase + i * EntrySize;
                    for (auto poolOff : PoolOffsets) {
                        auto* pTail = reinterpret_cast<std::uintptr_t*>(entry + poolOff + 8);
                        if (*pTail == 0 || *pTail < reinterpret_cast<std::uintptr_t>(newBase) ||
                            *pTail >= reinterpret_cast<std::uintptr_t>(newBase + newSize)) {
                            *pTail = reinterpret_cast<std::uintptr_t>(entry + poolOff);
                        }
                    }
                }

                // Swap pointer and update count
                *pBuf = reinterpret_cast<std::uintptr_t>(newBuf);
                *pCapacity = TargetCount;
                *pCount = TargetCount;
                g_vrExpanded = true;
                spdlog::info("[CascadeRuntime] VR array reallocated to {} entries at 0x{:X}",
                             TargetCount, reinterpret_cast<std::uintptr_t>(newBuf));
            }
        }

        // ----- TryRestoreMaskRotation -----
        // Restore full 4-cascade mask rotation once VR arrays are verified
        static void TryRestoreMaskRotation()
        {
            if (g_maskRestored) return;
            if (!g_vrExpanded) return;

            auto base = GetBase();

            // Validate render scene node exists
            auto renderNode = *reinterpret_cast<std::uintptr_t*>(base + SceneNodePtr);
            if (renderNode == 0) {
                spdlog::trace("[CascadeRuntime] Mask restore: render node null, waiting...");
                return;
            }

            // Validate cascade group exists
            auto cg = *reinterpret_cast<std::uintptr_t*>(renderNode + CascadeGroupOff);
            if (cg == 0) {
                spdlog::trace("[CascadeRuntime] Mask restore: cascade group null, waiting...");
                return;
            }

            // Validate flat array has 4+ entries with valid shadow maps
            auto flatCount  = *reinterpret_cast<std::uint32_t*>(cg + FlatCountOff);
            auto flatBuffer = *reinterpret_cast<std::uintptr_t*>(cg + FlatBufferOff);

            if (flatCount < 4 || flatBuffer == 0) {
                spdlog::trace("[CascadeRuntime] Mask restore: flat array not ready (count={}, buf=0x{:X})",
                              flatCount, flatBuffer);
                return;
            }

            // Validate shadow maps at +0x50 for each of 4 entries
            for (std::uint32_t i = 0; i < 4; i++) {
                auto entryAddr = flatBuffer + i * FlatEntrySize;
                auto shadowMap = *reinterpret_cast<std::uintptr_t*>(entryAddr + FlatShadowMapOff);
                if (shadowMap == 0) {
                    spdlog::trace("[CascadeRuntime] Mask restore: flat[{}] shadow map is null", i);
                    return;
                }
            }

            // Check all 4 crash prevention caves are non-null
            if (!g_nullSafetyCave || !g_nodeAllocCave || !g_entryZeroInitCave || !g_ptrValidationCave) {
                spdlog::warn("[CascadeRuntime] Mask restore: not all crash caves active "
                             "(null={}, node={}, zero={}, ptr={})",
                             g_nullSafetyCave != nullptr, g_nodeAllocCave != nullptr,
                             g_entryZeroInitCave != nullptr, g_ptrValidationCave != nullptr);
                return;
            }

            // Force VR flag before restoring masks
            ForceCascadeGroupVRFlag();

            // Clear flat[3]+0x102 last-cascade flag
            auto* lastCascadeFlag = reinterpret_cast<std::uint8_t*>(
                flatBuffer + 3 * FlatEntrySize + FlatLastCascadeOff);
            *lastCascadeFlag = 0;

            // Restore all 4 mask bytes from safe mode (0x03) to full rotation
            using namespace CascadeOffsets;
            int restored = 0;

            if (PatchByte(base + InitMask_Byte, InitMask_Safe, InitMask_Old,
                           "Restore InitMask 0x03->0x0F")) restored++;
            if (PatchByte(base + FallbackMask_Byte, FallbackMask_Safe, FallbackMask_Old,
                           "Restore FallbackMask 0x03->0x0F")) restored++;
            if (PatchByte(base + ArrayEntry1_Byte, ArrayEntry1_Safe, ArrayEntry1_Old,
                           "Restore ArrayEntry1 0x03->0x05")) restored++;
            if (PatchByte(base + ArrayEntry3_Byte, ArrayEntry3_Safe, ArrayEntry3_Old,
                           "Restore ArrayEntry3 0x03->0x09")) restored++;

            if (restored == 4) {
                g_maskRestored = true;
                spdlog::info("[CascadeRuntime] Mask rotation fully restored ({}/4 patches)", restored);
            } else {
                spdlog::warn("[CascadeRuntime] Mask restoration partial: {}/4 patches", restored);
            }
        }

        // ----- ClampMaskGlobal -----
        // While masks haven't been restored, clamp the runtime mask global to safe range
        static void ClampMaskGlobal()
        {
            if (g_maskRestored) return;

            auto base = GetBase();
            auto* pMask = reinterpret_cast<volatile std::uint32_t*>(base + MaskGlobal);
            std::uint32_t val = *pMask;
            if (val > 0x03) {
                *pMask = val & 0x03;
                spdlog::trace("[CascadeRuntime] Clamped mask global 0x{:X} -> 0x{:X}", val, val & 0x03);
            }
        }

        // ----- TimerCallback -----
        static VOID CALLBACK TimerCallback(PVOID /*lpParam*/, BOOLEAN /*TimerOrWaitFired*/)
        {
            g_tickCount++;

            __try {
                ForceCascadeCount4();
                ForceCascadeGroupVRFlag();
                FixSetupSceneNode();
                ForceShaderFields();
                ClampMaskGlobal();

                if (!g_maskRestored) {
                    TryExpandVRArray();
                    TryRestoreMaskRotation();
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::error("[CascadeRuntime] Exception in timer tick {} (code 0x{:08X})",
                              g_tickCount, GetExceptionCode());
            }

            // Self-delete timer after 60 ticks if mask restoration is complete
            if (g_maskRestored && g_tickCount >= 60) {
                spdlog::info("[CascadeRuntime] Mask restored and {} ticks elapsed, stopping timer",
                             g_tickCount);
                if (g_timerHandle) {
                    DeleteTimerQueueTimer(nullptr, g_timerHandle, nullptr);
                    g_timerHandle = nullptr;
                }
            }
        }
    }  // namespace CascadeRuntime

    // =========================================================================
    // StartCascadeRuntime — start 500ms timer for VR array expansion + mask restoration
    // =========================================================================
    void StartCascadeRuntime()
    {
        if (CascadeRuntime::g_timerHandle) {
            spdlog::warn("[EngineFixes] CascadeRuntime timer already running");
            return;
        }

        spdlog::info("[EngineFixes] Starting CascadeRuntime timer (2000ms delay, 500ms interval)...");

        BOOL ok = CreateTimerQueueTimer(
            &CascadeRuntime::g_timerHandle,
            nullptr,                           // default timer queue
            CascadeRuntime::TimerCallback,
            nullptr,                           // no parameter
            2000,                              // initial delay ms
            500,                               // interval ms
            WT_EXECUTEDEFAULT);

        if (!ok) {
            spdlog::error("[EngineFixes] CreateTimerQueueTimer failed (err {})", GetLastError());
            CascadeRuntime::g_timerHandle = nullptr;
        } else {
            spdlog::info("[EngineFixes] CascadeRuntime timer started");
        }
    }

    // =========================================================================
    // StopCascadeRuntime — cleanup timer on shutdown
    // =========================================================================
    void StopCascadeRuntime()
    {
        if (CascadeRuntime::g_timerHandle) {
            spdlog::info("[EngineFixes] Stopping CascadeRuntime timer...");
            DeleteTimerQueueTimer(nullptr, CascadeRuntime::g_timerHandle, INVALID_HANDLE_VALUE);
            CascadeRuntime::g_timerHandle = nullptr;
            spdlog::info("[EngineFixes] CascadeRuntime timer stopped");
        }
    }

    // =========================================================================
    // EnableTiledDeferredLighting — 5 NOP patches from F4VR-Tiled-Lighting
    // =========================================================================
    bool EnableTiledDeferredLighting()
    {
        using namespace TiledLightingOffsets;
        auto base = GetBase();
        int applied = 0;

        spdlog::info("[EngineFixes] Applying tiled deferred lighting patches (5 total)...");

        // 1. InitSDM: tile dimension gate (JZ -> NOP NOP)
        if (PatchByte(base + InitSDM_JZ1,     InitSDM_JZ1_Old1, NOP, "InitSDM JZ1 byte1") &&
            PatchByte(base + InitSDM_JZ1 + 1, InitSDM_JZ1_Old2, NOP, "InitSDM JZ1 byte2"))
            applied++;

        // 2. InitSDM: structured buffer creation gate (JZ near -> 6x NOP)
        if (PatchBytes(base + InitSDM_JZ2, InitSDM_JZ2_Old, 6, NOP, "InitSDM JZ2 (6-byte near)"))
            applied++;

        // 3. Render targets 0x6c/0x6d gate (JZ -> NOP NOP)
        if (PatchByte(base + RT_JZ1,     RT_JZ1_Old1, NOP, "RT JZ1 (0x6c/0x6d) byte1") &&
            PatchByte(base + RT_JZ1 + 1, RT_JZ1_Old2, NOP, "RT JZ1 (0x6c/0x6d) byte2"))
            applied++;

        // 4. Render targets 0x26/0x27 gate (JZ -> NOP NOP)
        if (PatchByte(base + RT_JZ2,     RT_JZ2_Old1, NOP, "RT JZ2 (0x26/0x27) byte1") &&
            PatchByte(base + RT_JZ2 + 1, RT_JZ2_Old2, NOP, "RT JZ2 (0x26/0x27) byte2"))
            applied++;

        // 5. Render dispatch VR mode skip (JNZ -> NOP NOP)
        if (PatchByte(base + Dispatch_JNZ,     Dispatch_JNZ_Old1, NOP, "Dispatch JNZ byte1") &&
            PatchByte(base + Dispatch_JNZ + 1, Dispatch_JNZ_Old2, NOP, "Dispatch JNZ byte2"))
            applied++;

        spdlog::info("[EngineFixes] Tiled deferred lighting: {}/5 patches applied", applied);
        return applied == 5;
    }

    // =========================================================================
    // ExpandShadowCascades — ~10 patches from VR-Shadow-Boost preloader
    // =========================================================================
    bool ExpandShadowCascades()
    {
        using namespace CascadeOffsets;
        auto base = GetBase();
        int applied = 0;
        int total = 0;

        spdlog::info("[EngineFixes] Applying shadow cascade expansion patches...");

        // 1. Force cascade count global to 4 (.data, no VirtualProtect needed)
        {
            total++;
            auto* p = reinterpret_cast<volatile std::uint32_t*>(base + CountGlobal);
            if (*p != DesiredCount) {
                *p = DesiredCount;
                spdlog::info("[EngineFixes]   Cascade count global: {} -> {}", *p, DesiredCount);
            } else {
                spdlog::info("[EngineFixes]   Cascade count global already {}", DesiredCount);
            }
            applied++;
        }

        // 2. Patch shader array capacity: 2 -> 4
        total++;
        if (PatchByte(base + ArrayCapByte, ArrayCapOld, ArrayCapNew,
                       "Shader array capacity 2->4"))
            applied++;

        // 3. Patch shader stored count: 2 -> 4
        total++;
        if (PatchByte(base + StoredCountByte, StoredCountOld, StoredCountNew,
                       "Shader stored count 2->4"))
            applied++;

        // 4. Patch CMP immediate at setup site: 2 -> 4
        //    Makes the comparison succeed (count==4), selecting .data shadow distance path
        total++;
        if (PatchByte(base + SetupCmpImm, SetupCmpOld, SetupCmpNew,
                       "Setup CMP imm 2->4 (redirect to .data distance)"))
            applied++;

        // 5. Patch 4 MOV [RIP+disp] instructions to MOV reg, 4 (immediate)
        struct { std::uintptr_t rva; const char* name; } movSites[] = {
            { CtorRead,    "Ctor read (FUN_1427e8f50)" },
            { SetupRead,   "Setup read (FUN_14290dbd0)" },
            { RenderRead1, "Render read 1 (FUN_1428a4a60)" },
            { RenderRead2, "Render read 2 (FUN_1428a4a60)" },
        };
        for (auto& site : movSites) {
            total++;
            if (PatchMovRipToImm(site.rva, CountGlobal, DesiredCount, site.name))
                applied++;
        }

        // 6. Write shadow distance to .data (NOT .rdata)
        // The CMP patch above (SetupCmpImm: 2->4) makes the function read from
        // ShadowDist2Cascade (.data, 0x3924808) instead of ShadowDist4Cascade (.rdata).
        // VR-Shadow-Boost v13.4+ abandoned the .rdata VirtualProtect approach.
        total++;
        {
            auto* pDist2 = reinterpret_cast<float*>(base + ShadowDist2Cascade);
            float origDist2 = *pDist2;

            if (origDist2 > 0.0f && origDist2 < 1e10f) {
                float newDist = origDist2 * 5.0f;
                *pDist2 = newDist;  // .data is RW, no VirtualProtect needed
                spdlog::info("[EngineFixes]   Shadow distance (.data): {:.1f} -> {:.1f}", origDist2, newDist);
                applied++;
            } else {
                // Fallback if dist2 is uninitialized
                *pDist2 = 15000.0f;
                spdlog::info("[EngineFixes]   Shadow distance (.data): fallback -> 15000.0");
                applied++;
            }
        }

        // 7. Apply mask writer safe mode (force all mask writes to 0x3)
        //    This prevents crashes while VR cascade arrays aren't ready.
        //    Full rotation (0xF on all frames) is restored by ApplyPostLoadFixes
        //    once all arrays are verified.
        {
            int maskPatches = 0;
            if (PatchByte(base + InitMask_Byte, InitMask_Old, InitMask_Safe,
                           "Initial mask 0xF->0x3")) maskPatches++;
            if (PatchByte(base + FallbackMask_Byte, FallbackMask_Old, FallbackMask_Safe,
                           "Fallback mask 0xF->0x3")) maskPatches++;
            if (PatchByte(base + ArrayEntry1_Byte, ArrayEntry1_Old, ArrayEntry1_Safe,
                           "Array[1] mask 0x5->0x3")) maskPatches++;
            if (PatchByte(base + ArrayEntry3_Byte, ArrayEntry3_Old, ArrayEntry3_Safe,
                           "Array[3] mask 0x9->0x3")) maskPatches++;
            spdlog::info("[EngineFixes]   Mask writer safe mode: {}/4 applied", maskPatches);
            total += 4;
            applied += maskPatches;
        }

        spdlog::info("[EngineFixes] Shadow cascade expansion: {}/{} patches applied", applied, total);
        return applied == total;
    }

    // =========================================================================
    // FixVRStereoShadowDispatch — JZ -> JMP at FUN_14281bd40+0xDC
    // RIGHT eye was skipping geometry marked by LEFT eye's deferred path.
    // =========================================================================
    bool FixVRStereoShadowDispatch()
    {
        using namespace StereoOffsets;
        auto base = GetBase();

        spdlog::info("[EngineFixes] Applying stereo dispatch fix (JZ->JMP)...");

        if (PatchByte(base + JzInstrRVA, JzOpcode, JmpOpcode,
                       "Stereo dispatch JZ->JMP at FUN_14281bd40+0xDC")) {
            spdlog::info("[EngineFixes] Stereo dispatch fix applied");
            return true;
        }

        spdlog::warn("[EngineFixes] Stereo dispatch fix FAILED");
        return false;
    }

    // =========================================================================
    // ApplyCrashPrevention — Code caves for null safety, zero-init, node clear,
    //                         and pointer validation
    // =========================================================================
    bool ApplyCrashPrevention()
    {
        using namespace CrashOffsets;
        auto base = GetBase();
        int applied = 0;
        int total = 4;

        spdlog::info("[EngineFixes] Applying crash prevention patches (4 code caves)...");

        // --- 1. Cascade entry zero-init (ROOT CAUSE fix, Step 9) ---
        // FUN_1427a51e0's "not found" path: zero tag entry fields and data entry
        // before writing the tag, preventing garbage cascade pointers
        {
            auto* patchAddr = reinterpret_cast<std::uint8_t*>(base + ZeroInit_RVA);
            auto returnAddr = base + ZeroInit_ReturnRVA;

            if (std::memcmp(patchAddr, ZeroInit_Expected, ZeroInit_Size) != 0) {
                spdlog::warn("[EngineFixes]   SKIP zero-init: bytes mismatch at RVA 0x{:X}", ZeroInit_RVA);
            } else {
                g_entryZeroInitCave = AllocateNearby(reinterpret_cast<std::uintptr_t>(patchAddr), 128);
                if (!g_entryZeroInitCave) {
                    spdlog::error("[EngineFixes]   FAIL zero-init: could not allocate code cave");
                } else {
                    auto* cave = reinterpret_cast<std::uint8_t*>(g_entryZeroInitCave);
                    int pos = 0;

                    // Code cave layout (from cascade_patch.cpp Step 9):
                    // Zero tag entry fields +0x08..+0x1F and data entry +0x00..+0x1F
                    // before the tag write instruction

                    // [0] push rcx
                    cave[pos++] = 0x51;

                    // [1] lea rcx, [rax + r10 + 0x90]
                    cave[pos++] = 0x4A; cave[pos++] = 0x8D; cave[pos++] = 0x8C; cave[pos++] = 0x10;
                    cave[pos++] = 0x90; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [9] mov qword [rcx+0x08], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x08;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [17] mov qword [rcx+0x10], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x10;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [25] mov qword [rcx+0x18], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x18;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [33] lea rcx, [rax + r10 + 0x130]
                    cave[pos++] = 0x4A; cave[pos++] = 0x8D; cave[pos++] = 0x8C; cave[pos++] = 0x10;
                    cave[pos++] = 0x30; cave[pos++] = 0x01; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [41] mov qword [rcx], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x01;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [48] mov qword [rcx+0x08], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x08;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [56] mov qword [rcx+0x10], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x10;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [64] mov qword [rcx+0x18], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x41; cave[pos++] = 0x18;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [72] pop rcx
                    cave[pos++] = 0x59;

                    // [73] mov [rax+r10+0x90], rdx (original instruction, relocated)
                    std::memcpy(cave + pos, ZeroInit_Expected, ZeroInit_Size);
                    pos += static_cast<int>(ZeroInit_Size);

                    // [81] jmp returnAddr
                    cave[pos++] = 0xE9;
                    std::int32_t relReturn = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(returnAddr) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &relReturn, 4);
                    pos += 4;

                    // Patch original: jmp code_cave (5 bytes) + 3 NOPs
                    std::uint8_t patch[8];
                    patch[0] = 0xE9;
                    std::int32_t jmpRel = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave)) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(patchAddr) + 5));
                    std::memcpy(patch + 1, &jmpRel, 4);
                    patch[5] = 0x90; patch[6] = 0x90; patch[7] = 0x90;

                    DWORD oldProtect;
                    if (VirtualProtect(patchAddr, ZeroInit_Size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        std::memcpy(patchAddr, patch, ZeroInit_Size);
                        VirtualProtect(patchAddr, ZeroInit_Size, oldProtect, &oldProtect);
                        FlushInstructionCache(GetCurrentProcess(), patchAddr, ZeroInit_Size);
                        spdlog::info("[EngineFixes]   Zero-init patch at RVA 0x{:X} -> cave 0x{:X} ({} bytes)",
                                     ZeroInit_RVA, reinterpret_cast<std::uintptr_t>(cave), pos);
                        applied++;
                    } else {
                        spdlog::error("[EngineFixes]   Zero-init: VirtualProtect failed");
                        VirtualFree(g_entryZeroInitCave, 0, MEM_RELEASE);
                        g_entryZeroInitCave = nullptr;
                    }
                }
            }
        }

        // --- 2. Null safety check (Step 7) ---
        // FUN_142813740: mov rbp,[r10+0x180] where r10 (param_2) can be NULL
        {
            auto* crashAddr = reinterpret_cast<std::uint8_t*>(base + NullSafety_RVA);
            auto returnAddr = reinterpret_cast<std::uintptr_t>(crashAddr) + NullSafety_Size;

            if (std::memcmp(crashAddr, NullSafety_Expected, NullSafety_Size) != 0) {
                spdlog::warn("[EngineFixes]   SKIP null safety: bytes mismatch at RVA 0x{:X}", NullSafety_RVA);
            } else {
                g_nullSafetyCave = AllocateNearby(reinterpret_cast<std::uintptr_t>(crashAddr), 64);
                if (!g_nullSafetyCave) {
                    spdlog::error("[EngineFixes]   FAIL null safety: could not allocate code cave");
                } else {
                    auto* cave = reinterpret_cast<std::uint8_t*>(g_nullSafetyCave);
                    int pos = 0;

                    // Code cave (from cascade_patch.cpp Step 7):
                    //   test r10, r10 -> jz null_case ->
                    //   mov rbp,[r10+0x180] -> test rbp,rbp -> jz done -> js null_case ->
                    //   done: jmp return -> null_case: xor ebp,ebp; jmp return

                    // [0] test r10, r10
                    cave[pos++] = 0x4D; cave[pos++] = 0x85; cave[pos++] = 0xD2;

                    // [3] jz null_case (offset 24, rel8 = 24-5 = 19 = 0x13)
                    cave[pos++] = 0x74; cave[pos++] = 0x13;

                    // [5] mov rbp, [r10+0x180] (original instruction)
                    std::memcpy(cave + pos, NullSafety_Expected, NullSafety_Size);
                    pos += static_cast<int>(NullSafety_Size);

                    // [12] test rbp, rbp
                    cave[pos++] = 0x48; cave[pos++] = 0x85; cave[pos++] = 0xED;

                    // [15] jz done (offset 19, rel8 = 19-17 = 2)
                    cave[pos++] = 0x74; cave[pos++] = 0x02;

                    // [17] js null_case (offset 24, rel8 = 24-19 = 5)
                    cave[pos++] = 0x78; cave[pos++] = 0x05;

                    // [19] done: jmp return_addr
                    cave[pos++] = 0xE9;
                    std::int32_t rel1 = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(returnAddr) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &rel1, 4);
                    pos += 4;

                    // [24] null_case: xor ebp, ebp
                    cave[pos++] = 0x31; cave[pos++] = 0xED;

                    // [26] jmp return_addr
                    cave[pos++] = 0xE9;
                    std::int32_t rel2 = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(returnAddr) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &rel2, 4);
                    pos += 4;

                    // Patch original: jmp cave (5 bytes) + 2 NOPs
                    std::uint8_t patch[7];
                    patch[0] = 0xE9;
                    std::int32_t jmpRel = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave)) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(crashAddr) + 5));
                    std::memcpy(patch + 1, &jmpRel, 4);
                    patch[5] = 0x90; patch[6] = 0x90;

                    DWORD oldProtect;
                    if (VirtualProtect(crashAddr, NullSafety_Size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        std::memcpy(crashAddr, patch, NullSafety_Size);
                        VirtualProtect(crashAddr, NullSafety_Size, oldProtect, &oldProtect);
                        FlushInstructionCache(GetCurrentProcess(), crashAddr, NullSafety_Size);
                        spdlog::info("[EngineFixes]   Null safety patch at RVA 0x{:X} -> cave 0x{:X}",
                                     NullSafety_RVA, reinterpret_cast<std::uintptr_t>(cave));
                        applied++;
                    } else {
                        spdlog::error("[EngineFixes]   Null safety: VirtualProtect failed");
                        VirtualFree(g_nullSafetyCave, 0, MEM_RELEASE);
                        g_nullSafetyCave = nullptr;
                    }
                }
            }
        }

        // --- 3. Node allocator clear (Step 8) ---
        // FUN_14278e610: clear +0x40 (->next) on node reuse to prevent linked list corruption
        {
            auto* funcAddr = reinterpret_cast<std::uint8_t*>(base + NodeAlloc_RVA);
            auto returnAddr = reinterpret_cast<std::uintptr_t>(funcAddr) + NodeAlloc_Size;

            if (std::memcmp(funcAddr, NodeAlloc_Expected, NodeAlloc_Size) != 0) {
                spdlog::warn("[EngineFixes]   SKIP node alloc: prologue mismatch at RVA 0x{:X}", NodeAlloc_RVA);
            } else {
                g_nodeAllocCave = AllocateNearby(reinterpret_cast<std::uintptr_t>(funcAddr), 64);
                if (!g_nodeAllocCave) {
                    spdlog::error("[EngineFixes]   FAIL node alloc: could not allocate code cave");
                } else {
                    auto* cave = reinterpret_cast<std::uint8_t*>(g_nodeAllocCave);
                    int pos = 0;

                    // Code cave (from cascade_patch.cpp Step 8):
                    //   test rdx,rdx -> jz skip -> mov qword [rdx+0x40], 0 ->
                    //   skip: sub rsp,0x68; mov r10,r9; jmp return

                    // [0] test rdx, rdx
                    cave[pos++] = 0x48; cave[pos++] = 0x85; cave[pos++] = 0xD2;

                    // [3] jz skip_clear (offset 13, rel8 = 13-5 = 8)
                    cave[pos++] = 0x74; cave[pos++] = 0x08;

                    // [5] mov qword ptr [rdx+0x40], 0
                    cave[pos++] = 0x48; cave[pos++] = 0xC7; cave[pos++] = 0x42; cave[pos++] = 0x40;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [13] sub rsp, 0x68 (relocated original)
                    cave[pos++] = 0x48; cave[pos++] = 0x83; cave[pos++] = 0xEC; cave[pos++] = 0x68;

                    // [17] mov r10, r9 (relocated original)
                    cave[pos++] = 0x4D; cave[pos++] = 0x8B; cave[pos++] = 0xD1;

                    // [20] jmp returnAddr
                    cave[pos++] = 0xE9;
                    std::int32_t rel1 = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(returnAddr) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &rel1, 4);
                    pos += 4;

                    // Patch original: jmp cave (5 bytes) + 2 NOPs
                    std::uint8_t patch[7];
                    patch[0] = 0xE9;
                    std::int32_t jmpRel = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave)) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(funcAddr) + 5));
                    std::memcpy(patch + 1, &jmpRel, 4);
                    patch[5] = 0x90; patch[6] = 0x90;

                    DWORD oldProtect;
                    if (VirtualProtect(funcAddr, NodeAlloc_Size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        std::memcpy(funcAddr, patch, NodeAlloc_Size);
                        VirtualProtect(funcAddr, NodeAlloc_Size, oldProtect, &oldProtect);
                        FlushInstructionCache(GetCurrentProcess(), funcAddr, NodeAlloc_Size);
                        spdlog::info("[EngineFixes]   Node alloc patch at RVA 0x{:X} -> cave 0x{:X}",
                                     NodeAlloc_RVA, reinterpret_cast<std::uintptr_t>(cave));
                        applied++;
                    } else {
                        spdlog::error("[EngineFixes]   Node alloc: VirtualProtect failed");
                        VirtualFree(g_nodeAllocCave, 0, MEM_RELEASE);
                        g_nodeAllocCave = nullptr;
                    }
                }
            }
        }

        // --- 4. Cascade pointer validation (Step 10) ---
        // FUN_1427a3f90+0xA4A: validate cascade pointer range, self-heal garbage
        {
            auto* patchAddr = reinterpret_cast<std::uint8_t*>(base + PtrValid_RVA);
            auto skipTarget = base + PtrValid_SkipTarget;
            auto continueAddr = base + PtrValid_Continue;

            if (std::memcmp(patchAddr, PtrValid_Expected, PtrValid_Size) != 0) {
                spdlog::warn("[EngineFixes]   SKIP ptr validation: bytes mismatch at RVA 0x{:X}", PtrValid_RVA);
            } else {
                g_ptrValidationCave = AllocateNearby(reinterpret_cast<std::uintptr_t>(patchAddr), 128);
                if (!g_ptrValidationCave) {
                    spdlog::error("[EngineFixes]   FAIL ptr validation: could not allocate code cave");
                } else {
                    auto* cave = reinterpret_cast<std::uint8_t*>(g_ptrValidationCave);
                    int pos = 0;

                    // Code cave (from cascade_patch.cpp Step 10):
                    // Self-healing pointer validation with upper-bits and lower-32-bits checks

                    // [0] test r14, r14
                    cave[pos++] = 0x4D; cave[pos++] = 0x85; cave[pos++] = 0xF6;

                    // [3] jz skip (offset 42, rel8 = 42-5 = 37 = 0x25)
                    cave[pos++] = 0x74; cave[pos++] = 0x25;

                    // [5] push rax
                    cave[pos++] = 0x50;

                    // [6] mov rax, r14
                    cave[pos++] = 0x4C; cave[pos++] = 0x89; cave[pos++] = 0xF0;

                    // [9] shr rax, 47
                    cave[pos++] = 0x48; cave[pos++] = 0xC1; cave[pos++] = 0xE8; cave[pos++] = 0x2F;

                    // [13] test eax, eax
                    cave[pos++] = 0x85; cave[pos++] = 0xC0;

                    // [15] jnz pop_fix (offset 30, rel8 = 30-17 = 13 = 0x0D)
                    cave[pos++] = 0x75; cave[pos++] = 0x0D;

                    // [17] mov eax, r14d
                    cave[pos++] = 0x44; cave[pos++] = 0x89; cave[pos++] = 0xF0;

                    // [20] test eax, eax
                    cave[pos++] = 0x85; cave[pos++] = 0xC0;

                    // [22] jz pop_fix (offset 30, rel8 = 30-24 = 6)
                    cave[pos++] = 0x74; cave[pos++] = 0x06;

                    // [24] pop rax — valid pointer path
                    cave[pos++] = 0x58;

                    // [25] jmp continue_addr
                    cave[pos++] = 0xE9;
                    std::int32_t relContinue = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(continueAddr) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &relContinue, 4);
                    pos += 4;

                    // [30] pop_fix: pop rax
                    cave[pos++] = 0x58;

                    // [31] mov qword ptr [r12], 0 — zero the cascade slot (self-heal)
                    cave[pos++] = 0x49; cave[pos++] = 0xC7; cave[pos++] = 0x04; cave[pos++] = 0x24;
                    cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00; cave[pos++] = 0x00;

                    // [39] xor r14d, r14d — r14 = 0 so NULL path creates new node
                    cave[pos++] = 0x45; cave[pos++] = 0x31; cave[pos++] = 0xF6;

                    // [42] skip: jmp skip_target
                    cave[pos++] = 0xE9;
                    std::int32_t relSkip = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(skipTarget) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave + pos) + 4));
                    std::memcpy(cave + pos, &relSkip, 4);
                    pos += 4;

                    // Patch original: jmp cave (5 bytes) + 4 NOPs
                    std::uint8_t patch[9];
                    patch[0] = 0xE9;
                    std::int32_t jmpRel = static_cast<std::int32_t>(
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(cave)) -
                        static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(patchAddr) + 5));
                    std::memcpy(patch + 1, &jmpRel, 4);
                    patch[5] = 0x90; patch[6] = 0x90; patch[7] = 0x90; patch[8] = 0x90;

                    DWORD oldProtect;
                    if (VirtualProtect(patchAddr, PtrValid_Size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        std::memcpy(patchAddr, patch, PtrValid_Size);
                        VirtualProtect(patchAddr, PtrValid_Size, oldProtect, &oldProtect);
                        FlushInstructionCache(GetCurrentProcess(), patchAddr, PtrValid_Size);
                        spdlog::info("[EngineFixes]   Ptr validation patch at RVA 0x{:X} -> cave 0x{:X} ({} bytes)",
                                     PtrValid_RVA, reinterpret_cast<std::uintptr_t>(cave), pos);
                        applied++;
                    } else {
                        spdlog::error("[EngineFixes]   Ptr validation: VirtualProtect failed");
                        VirtualFree(g_ptrValidationCave, 0, MEM_RELEASE);
                        g_ptrValidationCave = nullptr;
                    }
                }
            }
        }

        spdlog::info("[EngineFixes] Crash prevention: {}/{} code caves applied", applied, total);
        return applied == total;
    }

    // =========================================================================
    // ApplyAll — Phase 0: all early patches before game shader init
    // =========================================================================
    bool ApplyAll()
    {
        spdlog::info("[EngineFixes] ===== Phase 0: Early binary patches =====");

        bool tiledOk    = EnableTiledDeferredLighting();
        bool cascadesOk = ExpandShadowCascades();
        bool stereoOk   = FixVRStereoShadowDispatch();
        bool crashOk    = ApplyCrashPrevention();

        bool allOk = tiledOk && cascadesOk && stereoOk && crashOk;
        spdlog::info("[EngineFixes] Phase 0 complete: tiled={} cascades={} stereo={} crash={}",
                     tiledOk, cascadesOk, stereoOk, crashOk);
        return allOk;
    }

    // =========================================================================
    // ForceINISettings — bComputeShaderDeferredTiledLighting = true
    // =========================================================================
    bool ForceINISettings()
    {
        spdlog::info("[EngineFixes] Forcing INI settings...");

        auto* setting = RE::GetINISetting("bComputeShaderDeferredTiledLighting:Display");
        if (setting) {
            spdlog::info("[EngineFixes]   bComputeShaderDeferredTiledLighting = {} (current)",
                         setting->GetInt());
            setting->SetInt(1);
            spdlog::info("[EngineFixes]   bComputeShaderDeferredTiledLighting -> TRUE");
            return true;
        }

        spdlog::warn("[EngineFixes]   bComputeShaderDeferredTiledLighting setting not found");
        return false;
    }

    // =========================================================================
    // EnableSharedShadowMaps — RIGHT eye uses LEFT eye's shadow maps
    // Patches two displacement bytes (0x58 -> 0x50) so RIGHT eye reads the same
    // shadow map slots as LEFT eye, halving shadow map render cost in VR.
    // Must be applied post-load to avoid infinite loading screen.
    // =========================================================================
    bool EnableSharedShadowMaps()
    {
        using namespace CascadeOffsets;
        auto base = GetBase();
        int applied = 0;

        spdlog::info("[EngineFixes] Applying shared shadow maps patches...");

        if (PatchByte(base + SharedShadow_Activate, SharedShadow_Old, SharedShadow_New,
                       "SharedShadow: activate displacement 0x58->0x50"))
            applied++;

        if (PatchByte(base + SharedShadow_Dispatch, SharedShadow_Old, SharedShadow_New,
                       "SharedShadow: dispatch displacement 0x58->0x50"))
            applied++;

        spdlog::info("[EngineFixes] Shared shadow maps: {}/2 patches applied", applied);
        return applied == 2;
    }

    // =========================================================================
    // ApplyPostLoadFixes — Phase 1: after game load
    // =========================================================================
    bool ApplyPostLoadFixes()
    {
        spdlog::info("[EngineFixes] ===== Phase 1: Post-load fixes =====");

        bool iniOk    = ForceINISettings();
        bool sharedOk = EnableSharedShadowMaps();

        // Cascade runtime (VR array expansion, mask restoration) is handled by
        // StartCascadeRuntime() called separately after this function.

        spdlog::info("[EngineFixes] Phase 1 complete: ini={} shared_shadows={}", iniOk, sharedOk);
        return iniOk;
    }

}  // namespace EngineFixes
