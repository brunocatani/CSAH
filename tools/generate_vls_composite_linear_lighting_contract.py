from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


EXPECTED_FXP_SHA256 = (
    "eea46bb93c047c359451b8852a4ab013a1444d67fe8974813f5c456ef7b7d4ee"
)
EXPECTED_APPLICATION_IDENTITY = (
    5860,
    "520d02999b25d3e11f83c78528cfdc18",
)
EXPECTED_COMPOSITE_IDENTITY = (
    380,
    "d325285c3ccdf54bceea25b4769a1833",
)


class ContractError(RuntimeError):
    pass


def read_manifest(root: Path) -> dict[str, object]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "VLSCompositeLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != 1:
        raise ContractError("VLS composite manifest must contain one contract")
    entry = value[0]
    if not isinstance(entry, dict):
        raise ContractError("VLS composite manifest entry is not an object")
    if entry.get("imageSpaceIndex") != 98:
        raise ContractError("only ImageSpace block 98 may be replaced")
    name = entry.get("name")
    resource = entry.get("resource")
    if not isinstance(name, str) or not name:
        raise ContractError("VLS composite manifest name is invalid")
    if not isinstance(resource, str) or not resource.startswith("IDR_"):
        raise ContractError("VLS composite manifest resource is invalid")
    return entry


def fxp_shader(
    inventory: census.FxpInventory,
    image_space_index: int,
) -> census.DxbcContainer:
    family = f"ImageSpace[{image_space_index:03d}]"
    records = [
        item
        for item in inventory.containers
        if item.family == family and item.stage == "PS" and item.key == 0
    ]
    if len(records) != 1:
        raise ContractError(f"active FO4VR FXP must contain one {family} PS")
    return records[0]


def identity(container: census.DxbcContainer) -> tuple[int, str]:
    size, checksum = container.identity
    return int(size), str(checksum)


def vls_composite_original(root: Path) -> census.DxbcContainer:
    fxp_path = root / "Shaders012_VR.fxp"
    fxp_data = fxp_path.read_bytes()
    if hashlib.sha256(fxp_data).hexdigest() != EXPECTED_FXP_SHA256:
        raise ContractError("active FO4VR FXP hash changed")
    inventory = census.parse_fxp(fxp_data)
    application = fxp_shader(inventory, 97)
    composite = fxp_shader(inventory, 98)
    if identity(application) != EXPECTED_APPLICATION_IDENTITY:
        raise ContractError("ImageSpace block 97 is no longer VLS application")
    if identity(composite) != EXPECTED_COMPOSITE_IDENTITY:
        raise ContractError("ImageSpace block 98 is no longer VLS composite")
    matching_identities = [
        item for item in inventory.containers if item.identity == composite.identity
    ]
    if len(matching_identities) != 1:
        raise ContractError(
            "VLS composite pixel-shader identity is not unique in the active FXP"
        )
    return composite


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
    original: census.DxbcContainer,
    fxc: Path,
    output_directory: Path,
) -> bytes:
    source_directory = (
        root / "package" / "Shaders" / "Community" / "VLSCompositeLinearLighting"
    )
    source = source_directory / "VLSCompositeLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    for required in (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "LinearLightingVolumetricLighting(power.xxx)",
        "register(b2)",
        "register(t0)",
        "register(s0)",
    ):
        if required not in source_text:
            raise ContractError(
                f"VLS composite HLSL is missing required contract: {required}"
            )
    if "register(b8)" in source_text:
        raise ContractError("VLS composite HLSL must not consume geometry b8")

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
    if signature_contract(candidate_assembly) != signature_contract(original_assembly):
        raise ContractError("VLS composite candidate changed the FO4VR signature")

    original_fragments = (
        "dcl_constantbuffer CB2[2], immediateIndexed",
        "dcl_sampler s0",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "sample_indexable(texture2d)(float,float,float,float) r0.x",
        "mul o0.xyz, r0.xxxx, cb2[1].xyzx",
        "mov o0.w, l(1.000000)",
    )
    for fragment in original_fragments:
        if fragment not in original_assembly:
            raise ContractError(
                f"VLS composite original changed instruction contract: {fragment}"
            )

    candidate_declarations = census.parse_declarations(candidate_assembly)
    original_declarations = census.parse_declarations(original_assembly)
    candidate_buffers = dict(candidate_declarations.constant_buffers)
    original_buffers = dict(original_declarations.constant_buffers)
    if candidate_buffers.get(5) != 4:
        raise ContractError("VLS composite candidate does not consume frame b5[4]")
    if 8 in candidate_buffers:
        raise ContractError("VLS composite candidate unexpectedly consumes geometry b8")
    candidate_buffers.pop(5)
    if candidate_buffers != original_buffers:
        raise ContractError("VLS composite candidate changed vanilla constant buffers")
    if (
        candidate_declarations.samplers != original_declarations.samplers
        or candidate_declarations.textures != original_declarations.textures
    ):
        raise ContractError("VLS composite candidate changed texture/sampler bindings")
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
        "// Generated by tools/generate_vls_composite_linear_lighting_contract.py.",
        "// Do not edit this file by hand.",
        "constexpr VLSCompositeShaderContractDefinition kVLSCompositeShaderContract{",
        f'    "{entry["name"]}",',
        f'    {entry["imageSpaceIndex"]}u,',
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
        original = vls_composite_original(root)
        fxc = census.find_fxc(None)
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "VLSCompositeLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedVLSCompositeLinearLighting"
        )
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_vls_composite_linear_lighting_"
        ) as temporary:
            candidate = compile_candidate(
                root, str(entry["name"]), original, fxc, Path(temporary)
            )
        generated = render_contract(entry, original.data, candidate)
        output = arguments.output.resolve()
        candidate_asset = asset_directory / f'{entry["name"]}.dxbc'
        verified_asset = verified_directory / f'{entry["name"]}.dxbc'

        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            candidate_asset.write_bytes(candidate)
            verified_asset.write_bytes(original.data)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated VLS composite contract is stale: {output}")
            if not candidate_asset.is_file() or candidate_asset.read_bytes() != candidate:
                raise ContractError(
                    f"packaged VLS composite replacement is stale: {candidate_asset}"
                )
            if not verified_asset.is_file() or verified_asset.read_bytes() != original.data:
                raise ContractError(
                    f"verified VLS composite original is stale: {verified_asset}"
                )
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(
            f"VLS composite Linear Lighting contract generation failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
