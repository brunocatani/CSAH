from __future__ import annotations

import argparse
import json
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


def run_command(arguments: list[str], label: str) -> None:
    completed = subprocess.run(
        arguments,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout).strip()
        fail(f"{label} failed: {details}")


def run_fxc(fxc: Path, arguments: list[str]) -> None:
    run_command([str(fxc), "/nologo", *arguments], "fxc")


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
            line,
        ):
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
    field_pattern = re.compile(
        r"//\s+(?:uint|float)\s+(\w+);\s+// Offset:\s+(\d+)"
    )
    reflected = [
        (match.group(1), int(match.group(2)))
        for match in field_pattern.finditer(frame_block)
    ]
    if reflected != EXPECTED_FRAME_FIELDS:
        fail(f"LinearLightingFrame layout drifted: {reflected!r}")

    geometry_block = assembly[geometry_start:]
    if not re.search(
        r"float emissiveMult;\s+// Offset:\s+0 Size:\s+4", geometry_block
    ):
        fail("LinearLightingGeometry emissiveMult is not reflected at offset zero")


def load_contracts(root: Path) -> tuple[Path, list[dict[str, object]]]:
    manifest_path = (
        root / "package" / "Shaders" / "Community" / "LinearLightingContracts.json"
    )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, list) or len(manifest) != 37:
        fail("Linear Lighting manifest must contain exactly 37 contracts")

    reconstruction = root / "package" / "Shaders" / "Community" / "Reconstruction"
    verified = root / "package" / "Shaders" / "Community" / "VerifiedLinearLighting"
    names: set[str] = set()
    resources: set[str] = set()
    contracts: list[dict[str, object]] = []
    for index, entry in enumerate(manifest):
        if not isinstance(entry, dict):
            fail(f"manifest entry {index} is not an object")
        name = entry.get("name")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            fail(f"manifest entry {index} has an invalid name")
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            fail(f"manifest entry {index} has an invalid resource symbol")
        if name in names:
            fail(f"duplicate shader contract name: {name}")
        if resource in resources:
            fail(f"duplicate shader contract resource: {resource}")
        names.add(name)
        resources.add(resource)
        contracts.append(
            {
                "label": name,
                "resource": resource,
                "source": reconstruction
                / f"{name}.LinearLightingCandidate.hlsl",
                "packaged": reconstruction
                / f"{name}.LinearLightingCandidate.dxbc",
                "vanilla": verified / f"{name}.dxbc",
            }
        )
    return manifest_path, contracts


def verify_source_contracts(
    root: Path, contracts: list[dict[str, object]], shared: Path
) -> None:
    projected_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.hlsl"
    )
    six_mrt_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DefaultSixMrt_L4_00000002.LinearLightingCandidate.hlsl"
    )
    base_source_texts = [
        projected_source.read_text(encoding="utf-8"),
        six_mrt_source.read_text(encoding="utf-8"),
    ]
    shared_text = shared.read_text(encoding="utf-8")
    if any("enableGammaCorrection" in text for text in base_source_texts) or (
        "enableGammaCorrection" in shared_text
    ):
        fail("removed upstream setting enableGammaCorrection returned")

    for base_source_text in base_source_texts:
        for token in (
            "LinearLightingDiffuse(diffuse",
            "float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;",
        ):
            if token not in base_source_text:
                fail(f"active shader is missing transformation contract: {token}")
    if "LinearLightingEmitColor(cb2[1].xyz)" not in base_source_texts[0]:
        fail("projected shader is missing emission transformation")
    if "LinearLightingEmitColor(emitColor)" not in base_source_texts[1]:
        fail("six-MRT shader is missing emission transformation")
    for token in (
        "LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz)",
        "clip(alpha - cb2[1].w)",
    ):
        if token not in base_source_texts[1]:
            fail(f"six-MRT shader is missing semantic contract: {token}")
    if "clip(diffuse.w - cb2[1].w)" not in base_source_texts[0]:
        fail("projected shader no longer preserves alpha-reference testing")

    vertex_contracts = 0
    glowmap_contracts = 0
    instanced_contracts = 0
    model_space_normal_contracts = 0
    for contract in contracts:
        source = contract["source"]
        assert isinstance(source, Path)
        source_text = source.read_text(encoding="utf-8")
        if "L3" in str(contract["label"]) or "VertexColor" in str(contract["label"]):
            if "#define LINEAR_LIGHTING_VERTEX_COLOR 1" not in source_text:
                fail(f"{contract['label']} no longer selects the COLOR0 path")
            vertex_contracts += 1
        if str(contract["label"]).startswith("Glowmap"):
            if "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1" not in source_text:
                fail(f"{contract['label']} no longer selects textured emission")
            glowmap_contracts += 1
        if str(contract["label"]).startswith("Instanced"):
            if "#define LINEAR_LIGHTING_INSTANCED 1" not in source_text:
                fail(f"{contract['label']} no longer selects instanced material data")
            instanced_contracts += 1
        if str(contract["label"]).startswith("ModelSpaceNormals"):
            if "#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 1" not in source_text:
                fail(f"{contract['label']} no longer selects model-space normals")
            model_space_normal_contracts += 1
    if vertex_contracts != 17:
        fail(f"expected 17 COLOR0 contracts, found {vertex_contracts}")
    if glowmap_contracts != 10:
        fail(f"expected 10 glowmap contracts, found {glowmap_contracts}")
    if instanced_contracts != 2:
        fail(f"expected 2 instanced contracts, found {instanced_contracts}")
    if model_space_normal_contracts != 3:
        fail(
            "expected 3 model-space-normal contracts, "
            f"found {model_space_normal_contracts}"
        )


def verify(root: Path) -> None:
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
    generated_contracts = (
        root
        / "src"
        / "Features"
        / "linear_lighting"
        / "GeneratedLinearLightingContracts.inl"
    )
    resources_rc = root / "src" / "resources.rc"
    parity_source = root / "tests" / "LinearLightingShaderParityTests.cpp"
    generator = root / "tools" / "generate_linear_lighting_runtime_contracts.py"
    manifest_path, contracts = load_contracts(root)

    required_files = [
        manifest_path,
        shared,
        runtime_source,
        generated_contracts,
        resources_rc,
        parity_source,
        generator,
    ]
    for contract in contracts:
        required_files.extend(
            (contract["source"], contract["packaged"], contract["vanilla"])
        )
    for required in required_files:
        if not isinstance(required, Path) or not required.is_file():
            fail(f"required shader artifact is missing: {required}")

    verify_source_contracts(root, contracts, shared)
    runtime_text = runtime_source.read_text(encoding="utf-8")
    resources_text = resources_rc.read_text(encoding="utf-8")
    parity_text = parity_source.read_text(encoding="utf-8")
    parity_entries = re.findall(
        r'ShaderContract\{\s*"([^"]+)",\s*(\d+),\s*(true|false),\s*'
        r'(true|false)\s*\}',
        parity_text,
    )
    parity_contracts: dict[str, tuple[int, bool, bool]] = {}
    for name, mrt_count, has_vertex_color, is_instanced in parity_entries:
        if name in parity_contracts:
            fail(f"duplicate WARP parity contract: {name}")
        parity_contracts[name] = (
            int(mrt_count),
            has_vertex_color == "true",
            is_instanced == "true",
        )
    expected_names = {str(contract["label"]) for contract in contracts}
    if set(parity_contracts) != expected_names:
        missing = sorted(expected_names - set(parity_contracts))
        unexpected = sorted(set(parity_contracts) - expected_names)
        fail(
            "WARP parity manifest differs from runtime manifest: "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )
    for token in (
        "GeneratedLinearLightingContracts.inl",
        "kShaderContracts",
        "expectedChecksum.size()) == 0",
    ):
        if token not in runtime_text:
            fail(f"runtime shader identity gate is missing: {token}")

    run_command(
        [
            sys.executable,
            str(generator),
            "--root",
            str(root),
            "--output",
            str(generated_contracts),
            "--check",
        ],
        "Linear Lighting contract generator",
    )

    for contract in contracts:
        label = str(contract["label"])
        resource = str(contract["resource"])
        packaged = contract["packaged"]
        assert isinstance(packaged, Path)
        expected_resource = (
            f'{resource} RCDATA "../package/Shaders/Community/Reconstruction/'
            f'{packaged.name}"'
        )
        if expected_resource not in resources_text:
            fail(f"embedded resource path differs for {label}")

    fxc = find_fxc()
    with tempfile.TemporaryDirectory(prefix="fo4vr-cs-ll-") as temporary:
        temp = Path(temporary)
        for index, contract in enumerate(contracts):
            label = str(contract["label"])
            source = contract["source"]
            packaged = contract["packaged"]
            vanilla = contract["vanilla"]
            assert isinstance(source, Path)
            assert isinstance(packaged, Path)
            assert isinstance(vanilla, Path)
            compiled = temp / f"candidate-{index}.dxbc"
            candidate_assembly_path = temp / f"candidate-{index}.asm"
            vanilla_assembly_path = temp / f"vanilla-{index}.asm"
            run_fxc(
                fxc,
                [
                    "/T",
                    "ps_5_0",
                    "/E",
                    "PSMain",
                    "/O3",
                    "/Fo",
                    str(compiled),
                    "/Fc",
                    str(candidate_assembly_path),
                    str(source),
                ],
            )
            run_fxc(
                fxc,
                ["/dumpbin", "/Fc", str(vanilla_assembly_path), str(vanilla)],
            )

            compiled_bytes = compiled.read_bytes()
            if compiled_bytes != packaged.read_bytes():
                fail(f"{label} packaged DXBC is stale relative to source")
            if compiled_bytes == vanilla.read_bytes():
                fail(f"{label} replacement unexpectedly matches vanilla")

            candidate_assembly = candidate_assembly_path.read_text(
                encoding="utf-8", errors="replace"
            )
            vanilla_assembly = vanilla_assembly_path.read_text(
                encoding="utf-8", errors="replace"
            )
            candidate = parse_dcl_contract(candidate_assembly)
            original = parse_dcl_contract(vanilla_assembly)
            source_text = source.read_text(encoding="utf-8")
            expected_parity_metadata = (
                len(original["outputs"]),
                "#define LINEAR_LIGHTING_VERTEX_COLOR 1" in source_text,
                13 in original["constant_buffers"],
            )
            if parity_contracts[label] != expected_parity_metadata:
                fail(
                    f"{label} WARP parity metadata differs from shader reflection: "
                    f"{parity_contracts[label]!r} != {expected_parity_metadata!r}"
                )
            candidate_buffers = dict(candidate["constant_buffers"])
            frame_registers = candidate_buffers.pop(5, None)
            geometry_registers = candidate_buffers.pop(8, None)
            if frame_registers not in (5, 6):
                fail(f"{label} has an invalid LinearLightingFrame binding")
            if geometry_registers != 1:
                fail(f"{label} has an invalid LinearLightingGeometry binding")
            if candidate_buffers != original["constant_buffers"]:
                fail(
                    f"{label} replacement constant buffers differ from vanilla: "
                    f"{candidate_buffers!r} != {original['constant_buffers']!r}"
                )
            for key in ("samplers", "textures", "inputs", "outputs", "global_flags"):
                if candidate[key] != original[key]:
                    fail(f"{label} replacement {key} differs from vanilla")
            validate_frame_reflection(candidate_assembly)

    print(f"Linear Lighting shader contracts verified: {len(contracts)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        verify(arguments.root.resolve())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Linear Lighting shader verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
