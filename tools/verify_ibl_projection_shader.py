from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as shader_tools


class VerificationError(RuntimeError):
    pass


def compile_shader(root: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "IBL"
        / "DiffuseIblProjectionCS.hlsl"
    )
    if not source.is_file():
        raise VerificationError(f"IBL projection source is missing: {source}")
    fxc = shader_tools.find_fxc(None)
    with tempfile.TemporaryDirectory(prefix="fo4vr_ibl_projection_") as temporary:
        output = Path(temporary) / "DiffuseIblProjectionCS.dxbc"
        completed = subprocess.run(
            [
                str(fxc),
                "/nologo",
                "/T",
                "cs_5_0",
                "/E",
                "main",
                "/O3",
                "/Ges",
                "/WX",
                "/Fo",
                str(output),
                str(source),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            details = (completed.stdout + completed.stderr).strip()
            raise VerificationError(f"fxc failed for IBL projection: {details}")
        bytecode = output.read_bytes()
    if len(bytecode) < 20 or bytecode[:4] != b"DXBC":
        raise VerificationError("fxc did not produce a valid DXBC container")
    return bytecode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rebuild and verify the FO4VR diffuse IBL projection shader."
    )
    parser.add_argument("--root", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--write", action="store_true")
    arguments = parser.parse_args()

    try:
        root = arguments.root.resolve()
        asset = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "IBL"
            / "DiffuseIblProjectionCS.dxbc"
        )
        generated = compile_shader(root)
        if arguments.write:
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(generated)
            print(f"wrote {asset}")
            return 0
        if not asset.is_file():
            raise VerificationError(f"IBL projection asset is missing: {asset}")
        if asset.read_bytes() != generated:
            raise VerificationError(
                "IBL projection DXBC is stale; run this tool with --write"
            )
        print("IBL projection DXBC matches its HLSL source")
        return 0
    except (OSError, VerificationError, shader_tools.CensusError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
