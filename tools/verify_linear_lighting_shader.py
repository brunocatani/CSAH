from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_FRAME_FIELDS = [
    ("enableLinearLighting", 0),
    ("isDirLightLinear", 4),
    ("dirLightMult", 8),
    ("lightGamma", 12),
    ("colorGamma", 16),
    ("emitColorGamma", 20),
    ("glowmapGamma", 24),
    ("ambientGamma", 28),
    ("fogGamma", 32),
    ("fogAlphaGamma", 36),
    ("effectGamma", 40),
    ("effectAlphaGamma", 44),
    ("skyGamma", 48),
    ("waterGamma", 52),
    ("vlGamma", 56),
    ("vanillaDiffuseColorMult", 60),
    ("directionalLightMult", 64),
    ("pointLightMult", 68),
    ("ambientMult", 72),
    ("emitColorMult", 76),
    ("glowmapMult", 80),
    ("effectLightingMult", 84),
    ("membraneEffectMult", 88),
    ("bloodEffectMult", 92),
    ("projectedEffectMult", 96),
    ("deferredEffectMult", 100),
    ("otherEffectMult", 104),
    ("linearLightingPad0", 108),
]


def fail(message: str) -> None:
    raise RuntimeError(message)


def find_fxc() -> Path:
    candidates = [
        Path(r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe"),
        Path(r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\fxc.exe"),
    ]
    found = shutil.which("fxc.exe") or shutil.which("fxc")
    if found:
        candidates.insert(0, Path(found))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    fail("fxc.exe was not found")


def run_fxc(fxc: Path, arguments: list[str]) -> None:
    completed = subprocess.run(
        [str(fxc), "/nologo", *arguments],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout).strip()
        fail(f"fxc failed: {details}")


def parse_dcl_contract(assembly: str) -> dict[str, object]:
    constant_buffers: dict[int, int] = {}
    samplers: set[int] = set()
    textures: set[int] = set()
    inputs: list[str] = []
    outputs: set[int] = set()
    global_flags = ""

    for raw_line in assembly.splitlines():
        line = raw_line.strip()
        if line.startswith("dcl_globalFlags "):
            global_flags = line
        elif match := re.fullmatch(
                r"dcl_constantbuffer CB(\d+)\[(\d+)\], (?:immediate|dynamic)Indexed",
                line):
            constant_buffers[int(match.group(1))] = int(match.group(2))
        elif match := re.match(r"dcl_sampler s(\d+)", line):
            samplers.add(int(match.group(1)))
        elif match := re.match(r"dcl_resource_texture\w+.*\st(\d+)", line):
            textures.add(int(match.group(1)))
        elif match := re.match(r"dcl_input[^\s]*\s+(.+)", line):
            inputs.append(match.group(1))
        elif match := re.match(r"dcl_output o(\d+)", line):
            outputs.add(int(match.group(1)))

    return {
        "constant_buffers": constant_buffers,
        "samplers": sorted(samplers),
        "textures": sorted(textures),
        "inputs": inputs,
        "outputs": sorted(outputs),
        "global_flags": global_flags,
    }


def validate_frame_reflection(assembly: str) -> None:
    frame_start = assembly.find("// cbuffer LinearLightingFrame")
    geometry_start = assembly.find("// cbuffer LinearLightingGeometry")
    if frame_start < 0 or geometry_start <= frame_start:
        fail("Linear Lighting reflection blocks are missing")

    frame_block = assembly[frame_start:geometry_start]
    reflected: list[tuple[str, int]] = []
    field_pattern = re.compile(
        r"//\s+(?:uint|float)\s+(\w+);\s+// Offset:\s+(\d+)")
    for match in field_pattern.finditer(frame_block):
        reflected.append((match.group(1), int(match.group(2))))
    if reflected != EXPECTED_FRAME_FIELDS:
        fail(f"LinearLightingFrame layout drifted: {reflected!r}")

    geometry_block = assembly[geometry_start:]
    if not re.search(r"float emissiveMult;\s+// Offset:\s+0 Size:\s+4", geometry_block):
        fail("LinearLightingGeometry emissiveMult is not reflected at offset zero")


def verify(root: Path) -> None:
    reconstruction = root / "package" / "Shaders" / "Community" / "Reconstruction"
    verified = root / "package" / "Shaders" / "Community" / "VerifiedLinearLighting"
    shared = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "LinearLighting"
        / "LinearLighting.hlsli"
    )
    runtime_source = (
        root
        / "src"
        / "Features"
        / "linear_lighting"
        / "LinearLightingRuntime.cpp"
    )
    resources_rc = root / "src" / "resources.rc"
    contracts = [
        {
            "label": "DefaultProjectedFiveMrt_L4_00008002",
            "source": reconstruction
            / "DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultProjectedFiveMrt_L4_00008002.dxbc",
            "original_size": 3052,
            "replacement_size": 6184,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_PROJECTED_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
        },
        {
            "label": "DefaultProjectedFiveMrt_L3_00008003",
            "source": reconstruction
            / "DefaultProjectedFiveMrt_L3_00008003.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultProjectedFiveMrt_L3_00008003.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultProjectedFiveMrt_L3_00008003.dxbc",
            "original_size": 3120,
            "replacement_size": 6252,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_PROJECTED_VERTEX_COLOR_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
        },
        {
            "label": "DefaultSixMrt_L4_00000002",
            "source": reconstruction
            / "DefaultSixMrt_L4_00000002.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultSixMrt_L4_00000002.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultSixMrt_L4_00000002.dxbc",
            "original_size": 3336,
            "replacement_size": 6524,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "DefaultSixMrt_L3_00000003",
            "source": reconstruction
            / "DefaultSixMrt_L3_00000003.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultSixMrt_L3_00000003.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultSixMrt_L3_00000003.dxbc",
            "original_size": 3404,
            "replacement_size": 6592,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "DefaultModelSpaceSixMrt_L4_00000006",
            "source": reconstruction
            / "DefaultModelSpaceSixMrt_L4_00000006.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultModelSpaceSixMrt_L4_00000006.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultModelSpaceSixMrt_L4_00000006.dxbc",
            "original_size": 3336,
            "replacement_size": 6524,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_MODEL_SPACE_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "DefaultModelSpaceSixMrt_L3_00000007",
            "source": reconstruction
            / "DefaultModelSpaceSixMrt_L3_00000007.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultModelSpaceSixMrt_L3_00000007.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultModelSpaceSixMrt_L3_00000007.dxbc",
            "original_size": 3404,
            "replacement_size": 6592,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_MODEL_SPACE_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "DefaultDefShadowSixMrt_L4_00004002",
            "source": reconstruction
            / "DefaultDefShadowSixMrt_L4_00004002.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultDefShadowSixMrt_L4_00004002.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultDefShadowSixMrt_L4_00004002.dxbc",
            "original_size": 3388,
            "replacement_size": 6828,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_TEXTURED_EMISSION_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "frame_registers": 6,
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "DefaultDefShadowSixMrt_L3_00004003",
            "source": reconstruction
            / "DefaultDefShadowSixMrt_L3_00004003.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "DefaultDefShadowSixMrt_L3_00004003.LinearLightingCandidate.dxbc",
            "vanilla": verified / "DefaultDefShadowSixMrt_L3_00004003.dxbc",
            "original_size": 3456,
            "replacement_size": 6896,
            "resource": "IDR_LINEAR_LIGHTING_DEFAULT_TEXTURED_EMISSION_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "frame_registers": 6,
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "EnvmapSixMrt_L4_00000102",
            "source": reconstruction
            / "EnvmapSixMrt_L4_00000102.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapSixMrt_L4_00000102.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapSixMrt_L4_00000102.dxbc",
            "original_size": 3384,
            "replacement_size": 6572,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "EnvmapSixMrt_L3_00000103",
            "source": reconstruction
            / "EnvmapSixMrt_L3_00000103.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapSixMrt_L3_00000103.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapSixMrt_L3_00000103.dxbc",
            "original_size": 3460,
            "replacement_size": 6648,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "EnvmapModelSpaceSixMrt_L4_00000106",
            "source": reconstruction
            / "EnvmapModelSpaceSixMrt_L4_00000106.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapModelSpaceSixMrt_L4_00000106.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapModelSpaceSixMrt_L4_00000106.dxbc",
            "original_size": 3384,
            "replacement_size": 6572,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_MODEL_SPACE_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "EnvmapModelSpaceSixMrt_L3_00000107",
            "source": reconstruction
            / "EnvmapModelSpaceSixMrt_L3_00000107.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapModelSpaceSixMrt_L3_00000107.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapModelSpaceSixMrt_L3_00000107.dxbc",
            "original_size": 3460,
            "replacement_size": 6648,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_MODEL_SPACE_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
        },
        {
            "label": "EnvmapProjectedFiveMrt_L4_00008102",
            "source": reconstruction
            / "EnvmapProjectedFiveMrt_L4_00008102.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapProjectedFiveMrt_L4_00008102.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapProjectedFiveMrt_L4_00008102.dxbc",
            "original_size": 3100,
            "replacement_size": 6232,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_PROJECTED_FIVE_MRT_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
        },
        {
            "label": "EnvmapProjectedFiveMrt_L3_00008103",
            "source": reconstruction
            / "EnvmapProjectedFiveMrt_L3_00008103.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapProjectedFiveMrt_L3_00008103.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapProjectedFiveMrt_L3_00008103.dxbc",
            "original_size": 3176,
            "replacement_size": 6308,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_PROJECTED_FIVE_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
        },
        {
            "label": "EnvmapProjectedFiveMrt_L4_00008106",
            "source": reconstruction
            / "EnvmapProjectedFiveMrt_L4_00008106.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapProjectedFiveMrt_L4_00008106.LinearLightingCandidate.dxbc",
            "vanilla": verified / "EnvmapProjectedFiveMrt_L4_00008106.dxbc",
            "original_size": 3100,
            "replacement_size": 6232,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_PROJECTED_FIVE_MRT_NO_EARLY_DEPTH_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
        },
        {
            "label": "TexturedEmissionAlphaTestSixMrt_L4_00004102",
            "source": reconstruction
            / "TexturedEmissionAlphaTestSixMrt_L4_00004102.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "TexturedEmissionAlphaTestSixMrt_L4_00004102.LinearLightingCandidate.dxbc",
            "vanilla": verified
            / "TexturedEmissionAlphaTestSixMrt_L4_00004102.dxbc",
            "original_size": 3464,
            "replacement_size": 6904,
            "resource": "IDR_LINEAR_LIGHTING_TEXTURED_EMISSION_ALPHA_TEST_SIX_MRT_PS",
            "original_buffers": {2: 6, 12: 71},
            "frame_registers": 6,
            "outputs": [0, 1, 2, 3, 4, 5],
            "required_source_tokens": (
                "#define LINEAR_LIGHTING_ALPHA_TEST 1",
                "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1",
            ),
            "required_vanilla_tokens": (
                "add r0.z, r1.w, -cb2[1].w",
                "discard_nz r0.z",
                "mul o4.xyz, r1.xyzx, cb2[1].xyzx",
            ),
        },
        {
            "label": "TexturedEmissionAlphaTestSixMrt_L3_00004103",
            "source": reconstruction
            / "TexturedEmissionAlphaTestSixMrt_L3_00004103.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "TexturedEmissionAlphaTestSixMrt_L3_00004103.LinearLightingCandidate.dxbc",
            "vanilla": verified
            / "TexturedEmissionAlphaTestSixMrt_L3_00004103.dxbc",
            "original_size": 3540,
            "replacement_size": 6980,
            "resource": "IDR_LINEAR_LIGHTING_TEXTURED_EMISSION_ALPHA_TEST_SIX_MRT_VERTEX_COLOR_PS",
            "original_buffers": {2: 6, 12: 71},
            "frame_registers": 6,
            "outputs": [0, 1, 2, 3, 4, 5],
            "required_source_tokens": (
                "#define LINEAR_LIGHTING_ALPHA_TEST 1",
                "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1",
                "#define LINEAR_LIGHTING_VERTEX_COLOR 1",
            ),
            "required_vanilla_tokens": (
                "mad r0.z, r1.w, v6.w, -cb2[1].w",
                "discard_nz r0.z",
                "mul o4.xyz, r1.xyzx, cb2[1].xyzx",
            ),
        },
        {
            "label": "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016",
            "source": reconstruction
            / "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016.LinearLightingCandidate.dxbc",
            "vanilla": verified
            / "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016.dxbc",
            "original_size": 3452,
            "replacement_size": 6640,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_MODEL_SPACE_RGB_ONLY_ALPHA_TEST_PS",
            "original_buffers": {2: 6, 12: 71},
            "outputs": [0, 1, 2, 3, 4, 5],
            "required_source_tokens": (
                "#define LINEAR_LIGHTING_ALPHA_TEST 1",
                "#define LINEAR_LIGHTING_VERTEX_COLOR 1",
                "#define LINEAR_LIGHTING_VERTEX_ALPHA 0",
            ),
            "required_vanilla_tokens": (
                "dcl_input_ps linear v6.xyz",
                "add r0.z, r1.w, -cb2[1].w",
                "discard_nz r0.z",
                "mul r1.xyz, r1.xyzx, v6.xyzx",
            ),
        },
        {
            "label": "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5",
            "source": reconstruction
            / "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5.LinearLightingCandidate.hlsl",
            "packaged": reconstruction
            / "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5.LinearLightingCandidate.dxbc",
            "vanilla": verified
            / "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5.dxbc",
            "original_size": 3168,
            "replacement_size": 6300,
            "resource": "IDR_LINEAR_LIGHTING_ENVMAP_PROJECTED_RGB_ONLY_ALPHA_TEST_PS",
            "original_buffers": {2: 7, 12: 51},
            "outputs": [0, 1, 2, 3, 4],
            "required_source_tokens": (
                "#define LINEAR_LIGHTING_ALPHA_TEST 1",
                "#define LINEAR_LIGHTING_FORCE_EARLY_DEPTH 0",
                "#define LINEAR_LIGHTING_NORMAL_XY 1",
                "#define LINEAR_LIGHTING_VERTEX_COLOR 1",
                "#define LINEAR_LIGHTING_VERTEX_ALPHA 0",
            ),
            "required_vanilla_tokens": (
                "dcl_globalFlags refactoringAllowed",
                "dcl_input_ps linear v6.xyz",
                "add r0.z, r1.w, -cb2[1].w",
                "discard_nz r0.z",
                "mul r1.xyz, r1.xyzx, v6.xyzx",
            ),
        },
    ]

    required_files = [shared, runtime_source, resources_rc]
    for contract in contracts:
        required_files.extend(
            (contract["source"], contract["packaged"], contract["vanilla"]))
    for required in required_files:
        if not required.is_file():
            fail(f"required shader artifact is missing: {required}")

    base_source_texts = [
        contracts[index]["source"].read_text(encoding="utf-8")
        for index in (0, 2)
    ]
    vertex_source_texts = [
        contracts[index]["source"].read_text(encoding="utf-8")
        for index in (1, 3, 5, 7, 9, 11, 13, 16, 17, 18)
    ]
    shared_text = shared.read_text(encoding="utf-8")
    runtime_text = runtime_source.read_text(encoding="utf-8")
    resources_text = resources_rc.read_text(encoding="utf-8")
    if any("enableGammaCorrection" in text for text in base_source_texts) or \
            "enableGammaCorrection" in shared_text:
        fail("removed upstream setting enableGammaCorrection returned")
    for base_source_text in base_source_texts:
        for call in ("LinearLightingDiffuse(diffuse", "LinearLightingEmitColor(cb2[1].xyz)"):
            if call not in base_source_text:
                fail(f"active shader is missing transformation: {call}")
        if "diffuse *= input.vertexColor" not in base_source_text:
            fail("L3 shader no longer modulates sampled diffuse by COLOR0")
    for vertex_source_text in vertex_source_texts:
        if "#define LINEAR_LIGHTING_VERTEX_COLOR 1" not in vertex_source_text:
            fail("L3 shader no longer selects the verified COLOR0 path")
    if "LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz)" not in \
            base_source_texts[1]:
        fail("textured-emission shaders no longer transform the glow texture")
    if "clip(alpha - cb2[1].w)" not in base_source_texts[1]:
        fail("six-MRT envmap shaders no longer preserve alpha-reference testing")
    if "clip(diffuse.w - cb2[1].w)" not in base_source_texts[0]:
        fail("projected envmap shaders no longer preserve alpha-reference testing")
    for contract in contracts:
        source_text = contract["source"].read_text(encoding="utf-8")
        for token in contract.get("required_source_tokens", ()):
            if token not in source_text:
                fail(f"{contract['label']} is missing source contract: {token}")

    for token in ("kShaderContracts", "expectedChecksum.size()) == 0"):
        if token not in runtime_text:
            fail(f"runtime shader identity gate is missing: {token}")

    for index, contract in enumerate(contracts):
        original_bytes = contract["vanilla"].read_bytes()
        packaged_bytes = contract["packaged"].read_bytes()
        if len(original_bytes) != contract["original_size"]:
            fail(f"{contract['label']} vanilla byte length drifted")
        if len(packaged_bytes) != contract["replacement_size"]:
            fail(f"{contract['label']} replacement byte length drifted")

        section_start = runtime_text.find(f'"{contract["label"]}"')
        if section_start < 0:
            fail(f"runtime shader contract is missing: {contract['label']}")
        if index + 1 < len(contracts):
            section_end = runtime_text.find(
                f'"{contracts[index + 1]["label"]}"', section_start + 1)
        else:
            section_end = runtime_text.find("} };", section_start)
        if section_end <= section_start:
            fail(f"runtime shader contract boundary is malformed: {contract['label']}")
        section = runtime_text[section_start:section_end]
        if contract["resource"] not in section:
            fail(f"runtime resource mapping is missing for {contract['label']}")
        expected_resource = (
            f'{contract["resource"]} RCDATA '
            f'"../package/Shaders/Community/Reconstruction/'
            f'{contract["packaged"].name}"')
        if expected_resource not in resources_text:
            fail(f"embedded resource path differs for {contract['label']}")
        sizes = [
            int(value)
            for value in re.findall(r"\n\s+(\d+),\s*\n\s+\{", section)
        ]
        if sizes[:2] != [contract["original_size"], contract["replacement_size"]]:
            fail(f"runtime shader lengths differ for {contract['label']}: {sizes!r}")
        encoded = bytes(
            int(value, 16)
            for value in re.findall(
                r"std::byte\{\s*0x([0-9A-Fa-f]{2})\s*\}", section)
        )
        expected = original_bytes[4:20] + packaged_bytes[4:20]
        if encoded != expected:
            fail(f"runtime checksums differ for {contract['label']}")

    fxc = find_fxc()
    with tempfile.TemporaryDirectory(prefix="fo4vr-cs-ll-") as temporary:
        temp = Path(temporary)
        for index, contract in enumerate(contracts):
            compiled = temp / f"candidate-{index}.dxbc"
            candidate_assembly_path = temp / f"candidate-{index}.asm"
            vanilla_assembly_path = temp / f"vanilla-{index}.asm"
            run_fxc(
                fxc,
                [
                    "/T", "ps_5_0",
                    "/E", "PSMain",
                    "/O3",
                    "/Fo", str(compiled),
                    "/Fc", str(candidate_assembly_path),
                    str(contract["source"]),
                ],
            )
            run_fxc(
                fxc,
                ["/dumpbin", "/Fc", str(vanilla_assembly_path), str(contract["vanilla"])],
            )

            if compiled.read_bytes() != contract["packaged"].read_bytes():
                fail(f"{contract['label']} packaged DXBC is stale relative to source")
            if compiled.read_bytes() == contract["vanilla"].read_bytes():
                fail(f"{contract['label']} replacement unexpectedly matches vanilla")

            candidate_assembly = candidate_assembly_path.read_text(
                encoding="utf-8", errors="replace")
            vanilla_assembly = vanilla_assembly_path.read_text(
                encoding="utf-8", errors="replace")
            candidate = parse_dcl_contract(candidate_assembly)
            original = parse_dcl_contract(vanilla_assembly)

            if original["constant_buffers"] != contract["original_buffers"]:
                fail(
                    f"{contract['label']} vanilla constant buffers drifted: "
                    f"{original['constant_buffers']!r}")
            expected_candidate_buffers = dict(contract["original_buffers"])
            expected_candidate_buffers.update(
                {5: contract.get("frame_registers", 5), 8: 1})
            if candidate["constant_buffers"] != expected_candidate_buffers:
                fail(
                    f"{contract['label']} replacement constant buffers drifted: "
                    f"{candidate['constant_buffers']!r}")
            for key in ("samplers", "textures", "inputs", "outputs", "global_flags"):
                if candidate[key] != original[key]:
                    fail(f"{contract['label']} replacement {key} differs from vanilla")
            if candidate["outputs"] != contract["outputs"]:
                fail(f"{contract['label']} MRT contract drifted")
            for token in contract.get("required_vanilla_tokens", ()):
                if token not in vanilla_assembly:
                    fail(f"{contract['label']} vanilla semantic witness drifted: {token}")

            validate_frame_reflection(candidate_assembly)

    print(f"Linear Lighting shader contracts verified: {len(contracts)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        verify(arguments.root.resolve())
    except (OSError, RuntimeError) as error:
        print(f"Linear Lighting shader verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
