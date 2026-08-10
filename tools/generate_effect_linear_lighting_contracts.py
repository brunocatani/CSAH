from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


class ContractError(RuntimeError):
    pass


EXPECTED_IDENTITY = (932, "bda7028e4d023d80b1252229246173e7")
EXPECTED_DESCRIPTORS = {0, 0x10000000}


def read_manifest(root: Path) -> dict[str, object]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "EffectLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != 1:
        raise ContractError("Effect manifest must contain one contract")
    entry = value[0]
    if not isinstance(entry, dict):
        raise ContractError("Effect manifest entry is not an object")
    if entry.get("name") != "EffectDefault_00000000":
        raise ContractError("Effect manifest has an unexpected contract name")
    if entry.get("descriptor") != 0:
        raise ContractError("Effect manifest primary descriptor must be zero")
    if entry.get("aliases") != [0x10000000]:
        raise ContractError("Effect manifest aliases must contain 0x10000000")
    resource = entry.get("resource")
    if not isinstance(resource, str) or not resource.startswith("IDR_"):
        raise ContractError("Effect manifest has an invalid resource")
    return entry


def effect_original(root: Path) -> census.DxbcContainer:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    records = [
        item
        for item in inventory.containers
        if item.family == "Effect"
        and item.stage == "PS"
        and item.key in EXPECTED_DESCRIPTORS
    ]
    if len(records) != 2 or {int(item.key) for item in records} != EXPECTED_DESCRIPTORS:
        raise ContractError(
            "active FO4VR FXP must contain both verified basic Effect descriptors"
        )
    if any(item.identity != EXPECTED_IDENTITY for item in records):
        raise ContractError(
            "active FO4VR basic Effect identity changed from the verified contract"
        )
    return records[0]


def run_fxc(arguments: list[str], label: str) -> None:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        raise ContractError(f"fxc failed for {label}: {details}")


def signature_contract(assembly: str) -> str:
    try:
        start = assembly.index("// Input signature:")
        end = assembly.index("ps_5_0", start)
    except ValueError as error:
        raise ContractError("shader assembly is missing signature tables") from error
    return "\n".join(
        line.rstrip()
        for line in assembly[start:end].splitlines()
        if line.startswith("//")
    )


def compile_candidate(
    root: Path,
    original: census.DxbcContainer,
    fxc: Path,
    output_directory: Path,
) -> bytes:
    source_directory = (
        root / "package" / "Shaders" / "Community" / "EffectLinearLighting"
    )
    source = source_directory / "EffectLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "LinearLightingEffect(EffectBaseColor.xyz)",
        "LinearLightingEffect(EffectPropertyColor.xyz)",
        "LinearLightingEffect(input.color.xyz)",
        "EffectAlphaTest.y - sampledAlpha",
        "blendedColor *= otherEffectMult;",
        "LinearLightingEffectAlpha(alpha)",
    )
    for required in required_source:
        if required not in source_text:
            raise ContractError(f"Effect HLSL is missing contract: {required}")

    name = "EffectDefault_00000000"
    candidate_path = output_directory / f"{name}.dxbc"
    candidate_assembly_path = output_directory / f"{name}.asm.txt"
    original_path = output_directory / f"{name}.vanilla.dxbc"
    original_assembly_path = output_directory / f"{name}.vanilla.asm.txt"
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/I",
            str(source_directory),
            "/Fo",
            str(candidate_path),
            "/Fc",
            str(candidate_assembly_path),
            str(source),
        ],
        name,
    )
    original_path.write_bytes(original.data)
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/dumpbin",
            "/Fc",
            str(original_assembly_path),
            str(original_path),
        ],
        f"{name} vanilla",
    )

    candidate_assembly = candidate_assembly_path.read_text(encoding="utf-8")
    original_assembly = original_assembly_path.read_text(encoding="utf-8")
    candidate_signature = signature_contract(candidate_assembly)
    original_signature = signature_contract(original_assembly)
    if candidate_signature != original_signature:
        raise ContractError(
            "Effect candidate changed the exact FO4VR shader signature:\n"
            f"candidate:\n{candidate_signature}\noriginal:\n{original_signature}"
        )

    candidate_declarations = census.parse_declarations(candidate_assembly)
    original_declarations = census.parse_declarations(original_assembly)
    candidate_buffers = dict(candidate_declarations.constant_buffers)
    original_buffers = dict(original_declarations.constant_buffers)
    if candidate_buffers.get(5) != 7:
        raise ContractError("Effect candidate does not consume frame-only b5[7]")
    if 8 in candidate_buffers:
        raise ContractError("Effect candidate unexpectedly consumes geometry b8")
    candidate_buffers.pop(5)
    if candidate_buffers != original_buffers:
        raise ContractError("Effect candidate changed vanilla constant buffers")
    if (
        candidate_declarations.samplers != original_declarations.samplers
        or candidate_declarations.textures != original_declarations.textures
    ):
        raise ContractError("Effect candidate changed texture/sampler bindings")
    return candidate_path.read_bytes()


def format_identity(data: bytes, indent: str) -> list[str]:
    rows = [f"{indent}{{", f"{indent}    {len(data)},", f"{indent}    {{"]
    for offset in range(4, 20, 4):
        values = ", ".join(
            f"std::byte{{ 0x{value:02X} }}" for value in data[offset : offset + 4]
        )
        rows.append(f"{indent}        {values},")
    rows.extend((f"{indent}    }},", f"{indent}}},"))
    return rows


def render_contract(
    entry: dict[str, object], original: bytes, candidate: bytes
) -> str:
    rows = [
        "// Generated by tools/generate_effect_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        "constexpr std::array<EffectShaderContractDefinition, 1> "
        "kEffectShaderContracts{ {",
        "    {",
        f'        "{entry["name"]}",',
        f'        {entry["descriptor"]}u,',
        f'        {entry["resource"]},',
    ]
    rows.extend(format_identity(original, "        "))
    rows.extend(format_identity(candidate, "        "))
    rows.extend(("    },", "} };", ""))
    return "\n".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-assets", action="store_true")
    arguments = parser.parse_args()
    if arguments.check == arguments.write_assets:
        print("select exactly one of --check or --write-assets", file=sys.stderr)
        return 1

    try:
        root = arguments.root.resolve()
        entry = read_manifest(root)
        original = effect_original(root)
        fxc = census.find_fxc(None)
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "EffectLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedEffectLinearLighting"
        )
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_effect_linear_lighting_"
        ) as temporary:
            candidate = compile_candidate(
                root, original, fxc, Path(temporary)
            )
        generated = render_contract(entry, original.data, candidate)
        output = arguments.output.resolve()
        name = str(entry["name"])

        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            (asset_directory / f"{name}.dxbc").write_bytes(candidate)
            (verified_directory / f"{name}.dxbc").write_bytes(original.data)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated Effect contract is stale: {output}")
            candidate_asset = asset_directory / f"{name}.dxbc"
            if not candidate_asset.is_file() or candidate_asset.read_bytes() != candidate:
                raise ContractError(
                    f"packaged Effect replacement is stale: {candidate_asset}"
                )
            verified_asset = verified_directory / f"{name}.dxbc"
            if (
                not verified_asset.is_file()
                or verified_asset.read_bytes() != original.data
            ):
                raise ContractError(
                    f"verified Effect original is stale: {verified_asset}"
                )
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(
            f"Effect Linear Lighting contract generation failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
