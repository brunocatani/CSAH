from __future__ import annotations

import argparse
import hashlib
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import census_linear_lighting_fxp as census


EXPECTED_FXP_SHA256 = (
    "eea46bb93c047c359451b8852a4ab013a1444d67fe8974813f5c456ef7b7d4ee"
)
EXPECTED_DFPREPASS_RECORD_COUNT = 459
EXPECTED_DFPREPASS_IDENTITY_COUNT = 288
EXPECTED_SELECTED_RECORD_COUNT = 151
EXPECTED_SELECTED_IDENTITY_COUNT = 113
MATERIAL_TYPE_SHIFT = 8
MATERIAL_TYPE_MASK = 0x3F

FAMILY_ENVIRONMENT_MAP = "kEnvironmentMap"
FAMILY_PARALLAX = "kParallax"
FAMILY_PARALLAX_OCCLUSION = "kParallaxOcclusion"
FAMILY_MULTI_LAYER_PARALLAX = "kMultiLayerParallax"
FAMILY_EYE = "kEye"
FAMILY_LANDSCAPE_COMPLEX_PARALLAX = "kLandscapeComplexParallax"

MATERIAL_FAMILIES = {
    1: (FAMILY_ENVIRONMENT_MAP, 122, 86),
    3: (FAMILY_PARALLAX, 2, 2),
    7: (FAMILY_PARALLAX_OCCLUSION, 0, 0),
    11: (FAMILY_MULTI_LAYER_PARALLAX, 1, 1),
    16: (FAMILY_EYE, 22, 22),
}
EXCLUDED_MATERIAL_FAMILIES = {
    33: (6, 4),
}

# These exact type-zero landscape descriptors are the producer family proven
# by the local ENBliterator runtime test. Their bytecode identities are also
# independently required to exist in the authoritative FO4VR FXP below.
LANDSCAPE_COMPLEX_PARALLAX_DESCRIPTORS = frozenset(
    {
        0x00000023,
        0x02000023,
        0x0200003B,
        0x0A000023,
    }
)
EXPECTED_LANDSCAPE_IDENTITY_COUNT = 3


class ContractError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProducerContract:
    size: int
    checksum: str
    families: tuple[str, ...]

    @property
    def identity(self) -> tuple[int, str]:
        return self.size, self.checksum


@dataclass(frozen=True)
class ProducerAlias:
    descriptor: int
    contract_plus_one: int
    family: str


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate exact FO4VR DFPrepass complex-material producer "
            "contracts."
        )
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def material_type_from_descriptor(descriptor: int) -> int:
    if descriptor < 0 or descriptor > 0xFFFFFFFF:
        raise ContractError(f"descriptor is outside uint32: {descriptor}")
    return (descriptor >> MATERIAL_TYPE_SHIFT) & MATERIAL_TYPE_MASK


def _family_for_container(container: census.DxbcContainer) -> str | None:
    if container.key is None:
        raise ContractError("DFPrepass pixel shader is missing its descriptor")
    if container.key in LANDSCAPE_COMPLEX_PARALLAX_DESCRIPTORS:
        if material_type_from_descriptor(container.key) != 0:
            raise ContractError(
                "tested landscape descriptor escaped material type zero: "
                f"0x{container.key:08X}"
            )
        return FAMILY_LANDSCAPE_COMPLEX_PARALLAX
    material_type = material_type_from_descriptor(container.key)
    family = MATERIAL_FAMILIES.get(material_type)
    return family[0] if family is not None else None


def build_contracts(
    fxp_data: bytes,
) -> tuple[list[ProducerContract], list[ProducerAlias]]:
    digest = hashlib.sha256(fxp_data).hexdigest()
    if digest != EXPECTED_FXP_SHA256:
        raise ContractError(
            "Shaders012_VR.fxp identity changed: expected "
            f"{EXPECTED_FXP_SHA256}, found {digest}"
        )

    inventory = census.parse_fxp(fxp_data)
    dfprepass = [
        container
        for container in inventory.containers
        if container.family == "DFPrepass" and container.stage == "PS"
    ]
    if len(dfprepass) != EXPECTED_DFPREPASS_RECORD_COUNT:
        raise ContractError(
            "DFPrepass pixel-shader record count changed: expected "
            f"{EXPECTED_DFPREPASS_RECORD_COUNT}, found {len(dfprepass)}"
        )
    dfprepass_identities = {container.identity for container in dfprepass}
    if len(dfprepass_identities) != EXPECTED_DFPREPASS_IDENTITY_COUNT:
        raise ContractError(
            "DFPrepass pixel-shader identity count changed: expected "
            f"{EXPECTED_DFPREPASS_IDENTITY_COUNT}, found "
            f"{len(dfprepass_identities)}"
        )

    by_material_type: dict[int, list[census.DxbcContainer]] = defaultdict(list)
    by_descriptor: dict[int, census.DxbcContainer] = {}
    for container in dfprepass:
        if container.key is None:
            raise ContractError(
                "decoded DFPrepass pixel shader is missing its descriptor"
            )
        by_material_type[material_type_from_descriptor(container.key)].append(
            container
        )
        if container.key in by_descriptor:
            raise ContractError(
                f"duplicate DFPrepass descriptor 0x{container.key:08X}"
            )
        by_descriptor[container.key] = container

    for material_type, (_, expected_records, expected_identities) in (
        MATERIAL_FAMILIES.items()
    ):
        records = by_material_type[material_type]
        identity_count = len({container.identity for container in records})
        if len(records) != expected_records or identity_count != expected_identities:
            raise ContractError(
                f"material type {material_type} census changed: expected "
                f"{expected_records} records/{expected_identities} identities, "
                f"found {len(records)}/{identity_count}"
            )

    excluded_identities: set[tuple[int, str]] = set()
    for material_type, (expected_records, expected_identities) in (
        EXCLUDED_MATERIAL_FAMILIES.items()
    ):
        records = by_material_type[material_type]
        identities = {container.identity for container in records}
        if len(records) != expected_records or len(identities) != expected_identities:
            raise ContractError(
                f"excluded material type {material_type} census changed: expected "
                f"{expected_records} records/{expected_identities} identities, "
                f"found {len(records)}/{len(identities)}"
            )
        excluded_identities.update(identities)

    landscape = [
        by_descriptor[descriptor]
        for descriptor in sorted(LANDSCAPE_COMPLEX_PARALLAX_DESCRIPTORS)
        if descriptor in by_descriptor
    ]
    if len(landscape) != len(LANDSCAPE_COMPLEX_PARALLAX_DESCRIPTORS):
        missing = sorted(
            LANDSCAPE_COMPLEX_PARALLAX_DESCRIPTORS - by_descriptor.keys()
        )
        raise ContractError(
            "tested landscape descriptors are missing from the active FXP: "
            + ", ".join(f"0x{descriptor:08X}" for descriptor in missing)
        )
    if len({container.identity for container in landscape}) != (
        EXPECTED_LANDSCAPE_IDENTITY_COUNT
    ):
        raise ContractError(
            "tested landscape complex-parallax identity count changed"
        )

    selected = [
        container
        for container in dfprepass
        if _family_for_container(container) is not None
    ]
    selected_identities = {container.identity for container in selected}
    if len(selected) != EXPECTED_SELECTED_RECORD_COUNT:
        raise ContractError(
            "selected producer alias count changed: expected "
            f"{EXPECTED_SELECTED_RECORD_COUNT}, found {len(selected)}"
        )
    if len(selected_identities) != EXPECTED_SELECTED_IDENTITY_COUNT:
        raise ContractError(
            "selected producer identity count changed: expected "
            f"{EXPECTED_SELECTED_IDENTITY_COUNT}, found "
            f"{len(selected_identities)}"
        )
    overlap = selected_identities & excluded_identities
    if overlap:
        raise ContractError(
            "excluded type-33 identities leaked into the producer contract: "
            + ", ".join(
                f"{size}/{checksum}" for size, checksum in sorted(overlap)
            )
        )

    families_by_identity: dict[tuple[int, str], set[str]] = defaultdict(set)
    selected_rows: list[tuple[census.DxbcContainer, str]] = []
    for container in selected:
        family = _family_for_container(container)
        if family is None:
            raise ContractError("selected producer lost its family")
        families_by_identity[container.identity].add(family)
        selected_rows.append((container, family))

    contracts = [
        ProducerContract(size, checksum, tuple(sorted(families)))
        for (size, checksum), families in sorted(
            families_by_identity.items(), key=lambda item: (item[0][1], item[0][0])
        )
    ]
    index_by_identity = {
        contract.identity: index + 1 for index, contract in enumerate(contracts)
    }
    aliases = [
        ProducerAlias(
            descriptor=container.key if container.key is not None else 0,
            contract_plus_one=index_by_identity[container.identity],
            family=family,
        )
        for container, family in sorted(
            selected_rows,
            key=lambda row: row[0].key if row[0].key is not None else -1,
        )
    ]
    if len({alias.descriptor for alias in aliases}) != len(aliases):
        raise ContractError("selected producer aliases contain duplicate descriptors")
    return contracts, aliases


def _family_mask_expression(families: tuple[str, ...]) -> str:
    if not families:
        return "0"
    return " | ".join(
        f"producerFamilyMask(ComplexMaterialProducerFamily::{family})"
        for family in families
    )


def render_contracts(
    contracts: list[ProducerContract], aliases: list[ProducerAlias]
) -> str:
    lines = [
        "// Generated by tools/generate_complex_material_producer_contracts.py.",
        "// Do not edit this file by hand.",
        f"inline constexpr std::array<ComplexMaterialProducerContract, {len(contracts)}>",
        "    kComplexMaterialProducerContracts{ {",
    ]
    for contract in contracts:
        lines.extend(
            [
                "        {",
                f"            {contract.size},",
                f'            detail::dxbcChecksum("{contract.checksum}"),',
                f"            {_family_mask_expression(contract.families)},",
                "        },",
            ]
        )
    lines.extend(
        [
            "    } };",
            "",
            f"inline constexpr std::array<ComplexMaterialProducerAlias, {len(aliases)}>",
            "    kComplexMaterialProducerAliases{ {",
        ]
    )
    for alias in aliases:
        lines.append(
            "        { "
            f"0x{alias.descriptor:08X}u, {alias.contract_plus_one}, "
            f"ComplexMaterialProducerFamily::{alias.family} "
            "},"
        )
    lines.extend(["    } };", ""])
    return "\n".join(lines)


def write_or_check(output: Path, content: str, check: bool) -> None:
    if check:
        if not output.is_file():
            raise ContractError(f"generated contract is missing: {output}")
        current = output.read_text(encoding="utf-8")
        if current != content:
            raise ContractError(
                "generated complex-material producer contract is stale; run "
                "the generator without --check"
            )
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    contracts, aliases = build_contracts(
        (root / "Shaders012_VR.fxp").read_bytes()
    )
    write_or_check(
        arguments.output.resolve(),
        render_contracts(contracts, aliases),
        arguments.check,
    )
    print(
        "FO4VR complex-material producer contract verified: "
        f"{len(contracts)} identities/{len(aliases)} aliases; "
        "type-33 exclusion retained"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, census.CensusError, OSError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
