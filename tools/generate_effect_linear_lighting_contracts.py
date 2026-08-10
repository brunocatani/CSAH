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


EXPECTED_CONTRACTS = {
    0x00000000: ((932, "bda7028e4d023d80b1252229246173e7"), (0x10000000,)),
    0x00000001: ((1060, "bcf6976db0386f17e06b975e451db93d"), (0x10000001,)),
    0x00000004: ((944, "ff037555b0aee4168134f15de151515a"), (0x10000004,)),
    0x00000005: ((1088, "439de92c67b0352ee91fac96a615c4ab"), (0x10000005,)),
    0x00000020: ((924, "fdac9957717fcebecdd718ecb03c131d"), (0x10000020,)),
    0x00000021: ((1052, "26d01ba34d98cb8ee415ef63bcd543bf"), (0x10000021,)),
    0x00000024: ((936, "7f62b1b3a0e9e7e50adcd32b0df8ed06"), (0x10000024,)),
    0x00000025: ((1080, "50201203e8e3ca84c4074068586bee6d"), (0x10000025,)),
    0x00000040: ((1060, "3a83b4a97526664e309cd83a80d118bb"), ()),
    0x00000041: ((1188, "2861e98c14449b07a9417117995159bf"), ()),
    0x00000044: ((1072, "64f79a71a64e454ffcac5f7f6d54a520"), (0x10000044,)),
    0x00000045: ((1216, "6d5a39d1ea65c71b32099a86747bc715"), (0x10000045,)),
    0x40000000: ((960, "5a73a6524a1858ccd6804b02fe907563"), ()),
    0x40000001: ((1088, "931e56ea51a5477fa413958893baa10d"), ()),
    0x40000004: ((972, "4b5d61ebd80ff96a4b7998e9dedd676e"), (0x50000004,)),
    0x40000005: ((1116, "05f36a66e6c8d12f63503c21d9e739cc"), (0x50000005,)),
    0x40000020: ((952, "3f51717c58445d02f5c8cac045482865"), ()),
    0x40000021: ((1080, "22b153e376a940362f83ee9a2b7236e7"), (0x50000021,)),
    0x40000024: ((964, "37624a811e5382a9c6ae58adb78709ec"), (0x50000024,)),
    0x40000025: ((1108, "853a3f852ffce30c801adf1d29c68d22"), (0x50000025,)),
    0x40000045: ((1244, "5dac32845575e042be2c35d540f293a8"), (0x50000045,)),
    0x50000044: ((1100, "cb079e03a80dc618d30ceef20abaa0ba"), ()),
}


def read_manifest(root: Path) -> list[dict[str, object]]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "EffectLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != len(EXPECTED_CONTRACTS):
        raise ContractError("Effect manifest must contain twenty-two contracts")

    names: set[str] = set()
    descriptors: set[int] = set()
    resources: set[str] = set()
    for index, entry in enumerate(value):
        if not isinstance(entry, dict):
            raise ContractError(f"Effect manifest entry {index} is not an object")
        name = entry.get("name")
        descriptor = entry.get("descriptor")
        aliases = entry.get("aliases")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            raise ContractError(f"Effect manifest entry {index} has an invalid name")
        if not isinstance(descriptor, int) or descriptor not in EXPECTED_CONTRACTS:
            raise ContractError(
                f"Effect manifest entry {index} has an invalid descriptor"
            )
        expected_aliases = list(EXPECTED_CONTRACTS[descriptor][1])
        if aliases != expected_aliases:
            raise ContractError(
                f"Effect manifest entry {index} has unexpected descriptor aliases"
            )
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            raise ContractError(
                f"Effect manifest entry {index} has an invalid resource"
            )
        if name in names or descriptor in descriptors or resource in resources:
            raise ContractError(f"Effect manifest entry {index} is duplicated")
        names.add(name)
        descriptors.add(descriptor)
        resources.add(resource)
    if descriptors != set(EXPECTED_CONTRACTS):
        raise ContractError("Effect manifest descriptor matrix is incomplete")
    return sorted(value, key=lambda item: int(item["descriptor"]))


def effect_originals(root: Path) -> dict[int, census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    expected_keys = {
        key
        for descriptor, (_, aliases) in EXPECTED_CONTRACTS.items()
        for key in (descriptor, *aliases)
    }
    records = [
        item
        for item in inventory.containers
        if item.family == "Effect"
        and item.stage == "PS"
        and item.key in expected_keys
    ]
    if len(records) != len(expected_keys) or {
        int(item.key) for item in records
    } != expected_keys:
        raise ContractError(
            "active FO4VR FXP basic Effect descriptor matrix changed"
        )
    by_key = {int(item.key): item for item in records}
    originals: dict[int, census.DxbcContainer] = {}
    for descriptor, (identity, aliases) in EXPECTED_CONTRACTS.items():
        keys = (descriptor, *aliases)
        if any(by_key[key].identity != identity for key in keys):
            raise ContractError(
                f"active FO4VR Effect identity changed for 0x{descriptor:08X}"
            )
        originals[descriptor] = by_key[descriptor]
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
        root / "package" / "Shaders" / "Community" / "EffectLinearLighting"
    )
    source = source_directory / "EffectLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "LinearLightingEffect(baseColor.xyz)",
        "LinearLightingEffect(EffectPropertyColor.xyz)",
        "(EFFECT_TECHNIQUE & 0x1)",
        "(EFFECT_TECHNIQUE & 0x4)",
        "(EFFECT_TECHNIQUE & 0x20)",
        "(EFFECT_TECHNIQUE & 0x40)",
        "(EFFECT_TECHNIQUE & 0x40000000)",
        "LinearLightingFog(input.fogParam.xyz)",
        "LinearLightingFogAlpha(input.fogParam.w)",
        "EffectAlphaTest.y - sampledAlpha",
        "lightColor *= otherEffectMult;",
        "LinearLightingEffectAlpha(alpha)",
        "LinearLightingEffectVertexColor(input.vertexColor)",
    )
    for required in required_source:
        if required not in source_text:
            raise ContractError(f"Effect HLSL is missing contract: {required}")
    if source_text.index("lightColor *= otherEffectMult;") > source_text.index(
        "float3 blendedColor = lerp(lightColor, fogColor, fogFactor);"
    ):
        raise ContractError("Effect multiplier must be applied before fog blending")

    candidates: dict[int, bytes] = {}
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        name = str(entry["name"])
        original = originals[descriptor]
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
                f"EFFECT_TECHNIQUE=0x{descriptor:08X}",
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
                f"{name} changed the exact FO4VR shader signature:\n"
                f"candidate:\n{candidate_signature}\noriginal:\n{original_signature}"
            )

        candidate_declarations = census.parse_declarations(candidate_assembly)
        original_declarations = census.parse_declarations(original_assembly)
        candidate_buffers = dict(candidate_declarations.constant_buffers)
        original_buffers = dict(original_declarations.constant_buffers)
        if candidate_buffers.get(5) != 7:
            raise ContractError(f"{name} does not consume frame-only b5[7]")
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
        "// Generated by tools/generate_effect_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        "constexpr std::array<EffectShaderContractDefinition, 22> "
        "kEffectShaderContracts{ {",
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
        originals = effect_originals(root)
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
                name = str(entry["name"])
                (asset_directory / f"{name}.dxbc").write_bytes(
                    candidates[descriptor]
                )
                (verified_directory / f"{name}.dxbc").write_bytes(
                    originals[descriptor].data
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated Effect contracts are stale: {output}")
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                candidate_asset = asset_directory / f"{name}.dxbc"
                if (
                    not candidate_asset.is_file()
                    or candidate_asset.read_bytes() != candidates[descriptor]
                ):
                    raise ContractError(
                        f"packaged Effect replacement is stale: {candidate_asset}"
                    )
                verified_asset = verified_directory / f"{name}.dxbc"
                if (
                    not verified_asset.is_file()
                    or verified_asset.read_bytes() != originals[descriptor].data
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
