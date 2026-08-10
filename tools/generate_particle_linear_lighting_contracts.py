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


def read_manifest(root: Path) -> list[dict[str, object]]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ParticleLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != 4:
        raise ContractError("Particle manifest must contain four contracts")

    descriptors: set[int] = set()
    names: set[str] = set()
    resources: set[str] = set()
    for index, entry in enumerate(value):
        if not isinstance(entry, dict):
            raise ContractError(f"Particle manifest entry {index} is not an object")
        name = entry.get("name")
        descriptor = entry.get("descriptor")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            raise ContractError(f"Particle manifest entry {index} has an invalid name")
        if not isinstance(descriptor, int) or descriptor < 0 or descriptor > 3:
            raise ContractError(
                f"Particle manifest entry {index} has an invalid descriptor"
            )
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            raise ContractError(
                f"Particle manifest entry {index} has an invalid resource"
            )
        if name in names or descriptor in descriptors or resource in resources:
            raise ContractError(f"Particle manifest entry {index} is duplicated")
        names.add(name)
        descriptors.add(descriptor)
        resources.add(resource)
    if descriptors != set(range(4)):
        raise ContractError("Particle descriptors must be exactly 0 through 3")
    return sorted(value, key=lambda item: int(item["descriptor"]))


def particle_originals(root: Path) -> dict[int, census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    records = [
        item
        for item in inventory.containers
        if item.family == "Particle" and item.stage == "PS" and item.key is not None
    ]
    if len(records) != 6 or {int(item.key) for item in records} != set(range(6)):
        raise ContractError(
            "active FO4VR FXP must contain Particle PS descriptors 0 through 5"
        )
    originals = {int(item.key): item for item in records}
    if not (
        originals[0].identity
        == originals[4].identity
        == originals[5].identity
    ):
        raise ContractError(
            "Particle descriptors 0, 4, and 5 no longer share one identity"
        )
    if len({originals[index].identity for index in range(4)}) != 4:
        raise ContractError(
            "Particle descriptors 0 through 3 must have unique identities"
        )
    if len({item.identity for item in originals.values()}) != 4:
        raise ContractError("active FO4VR Particle PS inventory changed")
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


def compile_candidates(
    root: Path,
    manifest: list[dict[str, object]],
    originals: dict[int, census.DxbcContainer],
    fxc: Path,
    output_directory: Path,
) -> dict[int, bytes]:
    source_directory = (
        root / "package" / "Shaders" / "Community" / "ParticleLinearLighting"
    )
    source = source_directory / "ParticleLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "float ColorScale : packoffset(c0.x);",
        "baseColor.xyz = GrayscaleTexture.Sample(",
        "baseColor.w = GrayscaleTexture.Sample(",
        "baseColor.xyz = LinearLightingDiffuse(baseColor.xyz) * ColorScale;",
    )
    for required in required_source:
        if required not in source_text:
            raise ContractError(f"Particle HLSL is missing contract: {required}")
    if source_text.index("LinearLightingDiffuse(baseColor.xyz)") < source_text.index(
        "baseColor.xyz = GrayscaleTexture.Sample("
    ):
        raise ContractError(
            "Particle diffuse conversion must follow grayscale color replacement"
        )

    candidates: dict[int, bytes] = {}
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        name = str(entry["name"])
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
                "/D",
                f"PARTICLE_TECHNIQUE={descriptor}",
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
        original_path.write_bytes(originals[descriptor].data)
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
        if signature_contract(candidate_assembly) != signature_contract(
            original_assembly
        ):
            raise ContractError(f"{name} changed the exact FO4VR shader signature")

        candidate_declarations = census.parse_declarations(candidate_assembly)
        original_declarations = census.parse_declarations(original_assembly)
        candidate_buffers = dict(candidate_declarations.constant_buffers)
        original_buffers = dict(original_declarations.constant_buffers)
        if candidate_buffers.get(5) != 4:
            raise ContractError(f"{name} does not consume frame-only b5[4]")
        if 8 in candidate_buffers:
            raise ContractError(f"{name} unexpectedly consumes geometry b8")
        candidate_buffers.pop(5)
        if candidate_buffers != original_buffers:
            raise ContractError(f"{name} changed vanilla constant buffers")
        if (
            candidate_declarations.samplers != original_declarations.samplers
            or candidate_declarations.textures != original_declarations.textures
        ):
            raise ContractError(f"{name} changed texture/sampler bindings")
        candidates[descriptor] = candidate_path.read_bytes()
    return candidates


def format_identity(data: bytes, indent: str) -> list[str]:
    rows = [f"{indent}{{", f"{indent}    {len(data)},", f"{indent}    {{"]
    for offset in range(4, 20, 4):
        values = ", ".join(
            f"std::byte{{ 0x{value:02X} }}" for value in data[offset : offset + 4]
        )
        rows.append(f"{indent}        {values},")
    rows.extend((f"{indent}    }},", f"{indent}}},"))
    return rows


def render_contracts(
    manifest: list[dict[str, object]],
    originals: dict[int, census.DxbcContainer],
    candidates: dict[int, bytes],
) -> str:
    rows = [
        "// Generated by tools/generate_particle_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        "constexpr std::array<ParticleShaderContractDefinition, 4> "
        "kParticleShaderContracts{ {",
    ]
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        rows.extend(
            (
                "    {",
                f'        "{entry["name"]}",',
                f"        {descriptor}u,",
                f'        {entry["resource"]},',
            )
        )
        rows.extend(format_identity(originals[descriptor].data, "        "))
        rows.extend(format_identity(candidates[descriptor], "        "))
        rows.append("    },")
    rows.extend(("} };", ""))
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
        manifest = read_manifest(root)
        originals = particle_originals(root)
        fxc = census.find_fxc(None)
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "ParticleLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedParticleLinearLighting"
        )
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_particle_linear_lighting_"
        ) as temporary:
            candidates = compile_candidates(
                root, manifest, originals, fxc, Path(temporary)
            )
        generated = render_contracts(manifest, originals, candidates)
        output = arguments.output.resolve()

        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                (asset_directory / f'{entry["name"]}.dxbc').write_bytes(
                    candidates[descriptor]
                )
                (verified_directory / f'{entry["name"]}.dxbc').write_bytes(
                    originals[descriptor].data
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated Particle contracts are stale: {output}")
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                candidate_asset = asset_directory / f'{entry["name"]}.dxbc'
                if (
                    not candidate_asset.is_file()
                    or candidate_asset.read_bytes() != candidates[descriptor]
                ):
                    raise ContractError(
                        f"packaged Particle replacement is stale: {candidate_asset}"
                    )
                verified_asset = verified_directory / f'{entry["name"]}.dxbc'
                if (
                    not verified_asset.is_file()
                    or verified_asset.read_bytes() != originals[descriptor].data
                ):
                    raise ContractError(
                        f"verified Particle original is stale: {verified_asset}"
                    )
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(
            f"Particle Linear Lighting contract generation failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
