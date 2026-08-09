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

    for raw_line in assembly.splitlines():
        line = raw_line.strip()
        if match := re.fullmatch(r"dcl_constantbuffer CB(\d+)\[(\d+)\], immediateIndexed", line):
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
        },
    ]

    required_files = [shared, runtime_source, resources_rc]
    for contract in contracts:
        required_files.extend(
            (contract["source"], contract["packaged"], contract["vanilla"]))
    for required in required_files:
        if not required.is_file():
            fail(f"required shader artifact is missing: {required}")

    base_source_text = contracts[0]["source"].read_text(encoding="utf-8")
    vertex_source_text = contracts[1]["source"].read_text(encoding="utf-8")
    shared_text = shared.read_text(encoding="utf-8")
    runtime_text = runtime_source.read_text(encoding="utf-8")
    resources_text = resources_rc.read_text(encoding="utf-8")
    if "enableGammaCorrection" in base_source_text or "enableGammaCorrection" in shared_text:
        fail("removed upstream setting enableGammaCorrection returned")
    for call in ("LinearLightingDiffuse(diffuse.xyz)", "LinearLightingEmitColor(cb2[1].xyz)"):
        if call not in base_source_text:
            fail(f"active shader is missing transformation: {call}")
    if "#define LINEAR_LIGHTING_VERTEX_COLOR 1" not in vertex_source_text:
        fail("projected L3 shader no longer selects the verified COLOR0 path")
    if "diffuse *= input.vertexColor" not in base_source_text:
        fail("projected L3 shader no longer modulates sampled diffuse by COLOR0")

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

            if original["constant_buffers"] != {2: 7, 12: 51}:
                fail(
                    f"{contract['label']} vanilla constant buffers drifted: "
                    f"{original['constant_buffers']!r}")
            expected_candidate_buffers = {2: 7, 5: 5, 8: 1, 12: 51}
            if candidate["constant_buffers"] != expected_candidate_buffers:
                fail(
                    f"{contract['label']} replacement constant buffers drifted: "
                    f"{candidate['constant_buffers']!r}")
            for key in ("samplers", "textures", "inputs", "outputs"):
                if candidate[key] != original[key]:
                    fail(f"{contract['label']} replacement {key} differs from vanilla")
            if candidate["outputs"] != [0, 1, 2, 3, 4]:
                fail(f"{contract['label']} MRT contract drifted")

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
