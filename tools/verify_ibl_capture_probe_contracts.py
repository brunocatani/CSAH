from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


CONTRACT_PATTERN = re.compile(
    r"\{\s*(\d+)\s*,\s*"
    r'detail::dxbcChecksum\("([0-9a-fA-F]{32})"\)\s*\}'
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify DFComposite IBL capture-probe identities."
    )
    parser.add_argument("--root", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def is_environment_consumer(assembly: str) -> bool:
    return bool(
        re.search(r"dcl_resource_texturecubearray.*\st8$", assembly, re.MULTILINE)
        and re.search(r"dcl_sampler s8", assembly)
        and re.search(r"dcl_constantbuffer CB12\[", assembly)
        and re.search(r"dcl_output o0", assembly)
    )


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    header_path = root / "src" / "Features" / "ibl" / "IblCaptureProbeModel.h"
    fxp_path = root / "Shaders012_VR.fxp"
    if not header_path.is_file():
        fail(f"capture-probe model is missing: {header_path}")
    if not fxp_path.is_file():
        fail(f"local FXP authority is missing: {fxp_path}")

    contracts = [
        (int(size), checksum.lower())
        for size, checksum in CONTRACT_PATTERN.findall(
            header_path.read_text(encoding="utf-8")
        )
    ]
    if len(contracts) != 41 or len(set(contracts)) != 41:
        fail(
            "capture-probe contracts must contain exactly 41 unique identities; "
            f"found {len(contracts)}/{len(set(contracts))}"
        )

    inventory = census.parse_fxp(fxp_path.read_bytes())
    composite = [
        container
        for container in inventory.containers
        if container.family == "DFComposite" and container.stage == "PS"
    ]
    by_identity: dict[tuple[int, str], list[census.DxbcContainer]] = {}
    for container in composite:
        by_identity.setdefault(container.identity, []).append(container)
    missing = sorted(set(contracts) - set(by_identity))
    if missing:
        fail(f"capture-probe identities missing from DFComposite: {missing}")

    fxc = census.find_fxc(None)
    environment_identities: set[tuple[int, str]] = set()
    with tempfile.TemporaryDirectory(prefix="fo4vr-cs-ibl-probe-") as directory:
        temporary = Path(directory)
        for identity, occurrences in by_identity.items():
            shader_path = temporary / f"{identity[1]}-{identity[0]}.dxbc"
            shader_path.write_bytes(occurrences[0].data)
            if is_environment_consumer(census.disassemble(fxc, shader_path)):
                environment_identities.add(identity)

    contract_set = set(contracts)
    if environment_identities != contract_set:
        fail(
            "capture-probe coverage differs from exact TextureCubeArray-t8 "
            f"DFComposite consumers: missing={sorted(environment_identities - contract_set)}, "
            f"extra={sorted(contract_set - environment_identities)}"
        )
    alias_count = sum(len(by_identity[identity]) for identity in contract_set)
    if alias_count != 83:
        fail(f"expected 83 DFComposite aliases, found {alias_count}")

    print(
        "IBL capture-probe contracts verified: 41 exact DFComposite "
        "environment identities / 83 aliases."
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, census.CensusError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
