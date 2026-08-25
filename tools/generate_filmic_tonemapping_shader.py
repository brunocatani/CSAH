from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


EXPECTED_FAMILY = "ImageSpace[027]"
EXPECTED_IDENTITY = (1848, "f1bfa042d52062c4aedfd612bbf4799d")


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile the exact FO4VR HDR blend filmic replacement."
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    return parser.parse_args()


def run(command: list[str], label: str) -> None:
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} failed: {(result.stdout + result.stderr).strip()}"
        )


def verify_native_contract(root: Path) -> None:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    matches = [
        item
        for item in inventory.containers
        if item.family == EXPECTED_FAMILY
        and item.stage == "PS"
        and item.key == 0
        and item.identity == EXPECTED_IDENTITY
    ]
    all_identity_matches = [
        item for item in inventory.containers if item.identity == EXPECTED_IDENTITY
    ]
    if len(matches) != 1 or len(all_identity_matches) != 1:
        found = [
            (item.family, item.stage, item.key, item.identity)
            for item in all_identity_matches
        ]
        raise RuntimeError(
            "FO4VR HDR tonemap identity is no longer unique at "
            f"{EXPECTED_FAMILY}: {found}"
        )


def write_header(path: Path, data: bytes) -> None:
    rows = []
    for offset in range(0, len(data), 12):
        rows.append(
            "        "
            + ", ".join(f"0x{value:02X}" for value in data[offset : offset + 12])
            + ","
        )
    text = (
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace community_shaders::filmic_tonemapping::generated\n"
        "{\n"
        f"    inline constexpr std::array<std::uint8_t, {len(data)}> "
        "kPixelShader{\n"
        + "\n".join(rows)
        + "\n    };\n}\n"
    )
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    args = arguments()
    verify_native_contract(args.root)
    source = (
        args.root
        / "package"
        / "Shaders"
        / "Community"
        / "FilmicTonemapping"
        / "FilmicTonemappingPS.hlsl"
    )
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fo4vr-filmic-") as temporary:
        assembly = Path(temporary) / "FilmicTonemappingPS.asm.txt"
        run(
            [
                str(args.fxc),
                "/nologo",
                "/T",
                "ps_5_0",
                "/E",
                "PSMain",
                "/O3",
                "/Ges",
                "/WX",
                "/Fo",
                str(args.binary),
                "/Fc",
                str(assembly),
                str(source),
            ],
            "filmic tonemap compilation",
        )
        disassembly = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB2[6], immediateIndexed",
        "dcl_constantbuffer CB12[1], immediateIndexed",
        "dcl_constantbuffer CB13[1], immediateIndexed",
        "dcl_sampler s0, mode_default",
        "dcl_sampler s1, mode_default",
        "dcl_sampler s2, mode_default",
        "dcl_sampler s3, mode_default",
        "dcl_sampler s4, mode_default",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t1",
        "dcl_resource_texture2d (float,float,float,float) t2",
        "dcl_resource_texture2d (float,float,float,float) t3",
        "dcl_resource_texture2d (float,float,float,float) t4",
        "dcl_resource_texture2d (float,float,float,float) t5",
        "dcl_input_ps linear v1.xy",
        "dcl_output o0.xyzw",
    ):
        if required not in disassembly:
            raise RuntimeError(
                "compiled filmic shader changed its native linkage contract: "
                + required
            )
    data = args.binary.read_bytes()
    if data[:4] != b"DXBC":
        raise RuntimeError("compiled filmic shader is not DXBC")
    write_header(args.header, data)
    print(
        f"generated {len(data)}-byte filmic shader for the unique "
        f"{EXPECTED_FAMILY} contract"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
