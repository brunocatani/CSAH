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


def read_manifest(root: Path) -> dict[str, object]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "DistantTreeLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != 1:
        raise ContractError("DistantTree manifest must contain one contract")
    entry = value[0]
    if not isinstance(entry, dict):
        raise ContractError("DistantTree manifest entry is not an object")
    if entry.get("descriptor") != 0:
        raise ContractError("only color descriptor 0 may be replaced")
    name = entry.get("name")
    resource = entry.get("resource")
    if not isinstance(name, str) or not name:
        raise ContractError("DistantTree manifest name is invalid")
    if not isinstance(resource, str) or not resource.startswith("IDR_"):
        raise ContractError("DistantTree manifest resource is invalid")
    return entry


def distant_tree_originals(root: Path) -> dict[int, census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    records = [
        item
        for item in inventory.containers
        if item.family == "DistantTree"
        and item.stage == "PS"
        and item.key is not None
    ]
    if len(records) != 2 or {int(item.key) for item in records} != {0, 1}:
        raise ContractError(
            "active FO4VR FXP must contain DistantTree PS descriptors 0 and 1"
        )
    originals = {int(item.key): item for item in records}
    if len({item.identity for item in originals.values()}) != 2:
        raise ContractError("active DistantTree PS records must be unique")
    return originals


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
    name: str,
    originals: dict[int, census.DxbcContainer],
    fxc: Path,
    output_directory: Path,
) -> bytes:
    source_directory = (
        root / "package" / "Shaders" / "Community" / "DistantTreeLinearLighting"
    )
    source = source_directory / "DistantTreeLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    for required in (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "LinearLightingDiffuse(sampledDiffuse)",
        "LinearLightingDirectionalLight(",
        "LinearLightingAmbient(",
        "LinearLightingFog(input.fog.xyz)",
        "LinearLightingFogAlpha(input.fog.w)",
    ):
        if required not in source_text:
            raise ContractError(
                f"DistantTree HLSL is missing required contract: {required}"
            )

    candidate_path = output_directory / f"{name}.dxbc"
    candidate_assembly_path = output_directory / f"{name}.asm.txt"
    original_path = output_directory / f"{name}.vanilla.dxbc"
    original_assembly_path = output_directory / f"{name}.vanilla.asm.txt"
    depth_path = output_directory / "DistantTreeDepth_00000001.dxbc"
    depth_assembly_path = output_directory / "DistantTreeDepth_00000001.asm.txt"
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
    original_path.write_bytes(originals[0].data)
    depth_path.write_bytes(originals[1].data)
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
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/dumpbin",
            "/Fc",
            str(depth_assembly_path),
            str(depth_path),
        ],
        "DistantTree depth",
    )

    candidate_assembly = candidate_assembly_path.read_text(encoding="utf-8")
    original_assembly = original_assembly_path.read_text(encoding="utf-8")
    if signature_contract(candidate_assembly) != signature_contract(
        original_assembly
    ):
        raise ContractError("DistantTree candidate changed the FO4VR signature")

    candidate_declarations = census.parse_declarations(candidate_assembly)
    original_declarations = census.parse_declarations(original_assembly)
    candidate_buffers = dict(candidate_declarations.constant_buffers)
    original_buffers = dict(original_declarations.constant_buffers)
    if candidate_buffers.get(5) != 5:
        raise ContractError("DistantTree candidate does not consume frame b5[5]")
    if 8 in candidate_buffers:
        raise ContractError("DistantTree candidate unexpectedly consumes geometry b8")
    candidate_buffers.pop(5)
    if candidate_buffers != original_buffers:
        raise ContractError("DistantTree candidate changed vanilla constant buffers")
    if (
        candidate_declarations.samplers != original_declarations.samplers
        or candidate_declarations.textures != original_declarations.textures
    ):
        raise ContractError("DistantTree candidate changed texture/sampler bindings")

    depth_assembly = depth_assembly_path.read_text(encoding="utf-8")
    depth_declarations = census.parse_declarations(depth_assembly)
    if (
        depth_declarations.constant_buffers
        or depth_declarations.samplers
        or depth_declarations.textures
        or "div o0.xyz" not in depth_assembly
    ):
        raise ContractError(
            "DistantTree descriptor 1 is no longer the resource-free depth pass"
        )
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
    entry: dict[str, object],
    original: bytes,
    candidate: bytes,
) -> str:
    rows = [
        "// Generated by tools/generate_distant_tree_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        "constexpr DistantTreeShaderContractDefinition kDistantTreeShaderContract{",
        f'    "{entry["name"]}",',
        f'    {entry["descriptor"]}u,',
        f'    {entry["resource"]},',
    ]
    rows.extend(format_identity(original, "    "))
    rows.extend(format_identity(candidate, "    "))
    rows.extend(("};", ""))
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
        originals = distant_tree_originals(root)
        fxc = census.find_fxc(None)
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "DistantTreeLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedDistantTreeLinearLighting"
        )
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_distant_tree_linear_lighting_"
        ) as temporary:
            candidate = compile_candidate(
                root, str(entry["name"]), originals, fxc, Path(temporary)
            )
        generated = render_contract(entry, originals[0].data, candidate)
        output = arguments.output.resolve()
        candidate_asset = asset_directory / f'{entry["name"]}.dxbc'
        verified_asset = verified_directory / f'{entry["name"]}.dxbc'

        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            candidate_asset.write_bytes(candidate)
            verified_asset.write_bytes(originals[0].data)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated DistantTree contract is stale: {output}")
            if not candidate_asset.is_file() or candidate_asset.read_bytes() != candidate:
                raise ContractError(
                    f"packaged DistantTree replacement is stale: {candidate_asset}"
                )
            if not verified_asset.is_file() or verified_asset.read_bytes() != originals[0].data:
                raise ContractError(
                    f"verified DistantTree original is stale: {verified_asset}"
                )
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(
            f"DistantTree Linear Lighting contract generation failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
