#!/usr/bin/env python3
"""Build a deterministic FO4VR Community Shaders authored-RMAOS companion mod.

BA2/BSA extraction is deliberately outside this tool. Feed it loose staging
directories produced through the workspace's authoritative archive extractor,
ordered from lowest to highest precedence.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from typing import Iterable, Sequence

from PIL import Image


RMAOS_NAMESPACE = "textures\\fo4vrcommunityshaders\\authored_pbr"
ROUGHNESS_LUT = tuple(max(10, min(255, 255 - value)) for value in range(256))
F0_LUT = tuple(round(5 + value * 15 / 255) for value in range(256))

METAL_EXCLUSIONS = (
    "glass",
    "window",
    "rock",
    "stone",
    "concrete",
    "water",
    "wood",
    "cloth",
    "skin",
    "ground",
    "terrain",
    "grass",
    "leaf",
    "plant",
    "soil",
    "sand",
    "snow",
)
METAL_MARKERS = (
    "metal",
    "steel",
    "copper",
    "brass",
    "bronze",
    "chrome",
    "aluminum",
    "aluminium",
    "silver",
    "gold",
    "blade",
    "powerarmor",
    "power_armor",
    "\\weapons\\",
    "\\vehicles\\",
    "\\actors\\handy\\",
    "\\actors\\protectron\\",
    "\\actors\\assaultron\\",
    "\\actors\\sentrybot\\",
    "\\actors\\robot\\",
    "\\setdressing\\statues\\",
)


@dataclasses.dataclass(frozen=True)
class Layer:
    name: str
    root: Path


@dataclasses.dataclass(frozen=True)
class SourceFile:
    layer: str
    path: Path


@dataclasses.dataclass(frozen=True)
class MaterialInput:
    base_path: str
    rmaos_path: str
    specular: SourceFile | None
    environment_mask: SourceFile | None
    metallic_mask_enabled: bool


@dataclasses.dataclass(frozen=True)
class DdsMetadata:
    width: int
    height: int
    mip_count: int
    dxgi_format: int


def canonical_texture_path(relative: Path | str) -> str:
    value = str(relative).replace("/", "\\").lower()
    while value.startswith(".\\"):
        value = value[2:]
    if not value.startswith("textures\\") or ".." in value.split("\\"):
        raise ValueError(f"not a safe Data texture path: {relative}")
    return value


def parse_named_path(value: str) -> tuple[str, Path]:
    name, separator, raw_path = value.partition("=")
    if not separator or not name.strip() or not raw_path.strip():
        raise argparse.ArgumentTypeError("expected NAME=PATH")
    return name.strip(), Path(raw_path).resolve()


def is_high_confidence_metal(base_path: str, has_mask: bool) -> bool:
    if not has_mask:
        return False
    lowered = base_path.lower()
    filename = lowered.rsplit("\\", 1)[-1]
    if any(marker in filename for marker in METAL_EXCLUSIONS):
        return False
    return any(marker in lowered for marker in METAL_MARKERS)


def scan_layers(layers: Sequence[Layer]) -> list[MaterialInput]:
    effective: dict[str, SourceFile] = {}
    for layer in layers:
        if not layer.root.is_dir():
            raise FileNotFoundError(f"layer directory is missing: {layer.root}")
        for path in sorted(
            (candidate for candidate in layer.root.rglob("*") if candidate.is_file()),
            key=lambda candidate: str(candidate).lower(),
        ):
            lowered = path.name.lower()
            if not (lowered.endswith("_s.dds") or lowered.endswith("_m.dds")):
                continue
            relative = canonical_texture_path(path.relative_to(layer.root))
            effective[relative] = SourceFile(layer.name, path)

    stems: dict[str, dict[str, SourceFile]] = {}
    for path, source in effective.items():
        kind = path[-5]
        stem = path[:-6]
        stems.setdefault(stem, {})[kind] = source

    materials: list[MaterialInput] = []
    for stem in sorted(stems):
        sources = stems[stem]
        base_path = f"{stem}_d.dds"
        relative = base_path.removeprefix("textures\\")
        rmaos_path = f"{RMAOS_NAMESPACE}\\{relative[:-6]}_rmaos.dds"
        mask = sources.get("m")
        materials.append(
            MaterialInput(
                base_path=base_path,
                rmaos_path=rmaos_path,
                specular=sources.get("s"),
                environment_mask=mask,
                metallic_mask_enabled=is_high_confidence_metal(
                    base_path, mask is not None
                ),
            )
        )
    return materials


def _channel(image: Image.Image, index: int, size: tuple[int, int]) -> Image.Image:
    rgba = image.convert("RGBA")
    result = rgba.getchannel(index)
    if result.size != size:
        result = result.resize(size, Image.Resampling.LANCZOS)
    return result


def compose_rmaos(material: MaterialInput) -> Image.Image:
    specular_image = (
        Image.open(material.specular.path) if material.specular is not None else None
    )
    mask_image = (
        Image.open(material.environment_mask.path)
        if material.environment_mask is not None
        else None
    )
    if specular_image is None and mask_image is None:
        raise ValueError(f"material has no source maps: {material.base_path}")
    try:
        size = specular_image.size if specular_image is not None else mask_image.size
        if specular_image is not None:
            roughness = _channel(specular_image, 1, size).point(ROUGHNESS_LUT)
            dielectric_f0 = _channel(specular_image, 0, size).point(F0_LUT)
        else:
            roughness = Image.new("L", size, 153)
            dielectric_f0 = Image.new("L", size, 10)
        if material.metallic_mask_enabled and mask_image is not None:
            metalness = _channel(mask_image, 0, size)
        else:
            metalness = Image.new("L", size, 0)
        ambient_occlusion = Image.new("L", size, 255)
        return Image.merge(
            "RGBA", (roughness, metalness, ambient_occlusion, dielectric_f0)
        )
    finally:
        if specular_image is not None:
            specular_image.close()
        if mask_image is not None:
            mask_image.close()


def _relative_rmaos_path(path: str) -> Path:
    return Path(*path.split("\\"))


def _write_intermediate(
    material: MaterialInput, intermediate_root: Path
) -> tuple[MaterialInput, Path, tuple[int, int]]:
    relative = _relative_rmaos_path(material.rmaos_path).with_suffix(".png")
    destination = intermediate_root / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    image = compose_rmaos(material)
    try:
        image.save(destination, format="PNG", compress_level=1)
        return material, destination, image.size
    finally:
        image.close()


def _run_texconv(
    texconv: Path,
    source_directory: Path,
    output_directory: Path,
) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    command = [
        str(texconv),
        "-nologo",
        "-y",
        "-m",
        "0",
        "-f",
        "BC7_UNORM",
        "-bcquick",
        "-o",
        str(output_directory),
        str(source_directory / "*.png"),
    ]
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        detail = (completed.stdout + "\n" + completed.stderr).strip()
        raise RuntimeError(f"texconv failed for {source_directory}: {detail}")


def read_dds_metadata(path: Path) -> DdsMetadata:
    with path.open("rb") as stream:
        header = stream.read(148)
    if len(header) < 148 or header[:4] != b"DDS ":
        raise ValueError(f"invalid DDS header: {path}")
    (height,) = struct.unpack_from("<I", header, 12)
    (width,) = struct.unpack_from("<I", header, 16)
    (mip_count,) = struct.unpack_from("<I", header, 28)
    four_cc = header[84:88]
    if four_cc != b"DX10":
        raise ValueError(f"DDS lacks a DX10 header: {path}")
    (dxgi_format,) = struct.unpack_from("<I", header, 128)
    return DdsMetadata(width, height, max(1, mip_count), dxgi_format)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _find_texconv_output(directory: Path, stem: str) -> Path:
    for suffix in (".DDS", ".dds"):
        candidate = directory / f"{stem}{suffix}"
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"texconv did not create {stem}.dds under {directory}")


def build_pack(
    layers: Sequence[Layer],
    output: Path,
    texconv: Path,
    source_archives: Sequence[tuple[str, Path]],
    workers: int,
) -> dict[str, object]:
    if output.exists():
        raise FileExistsError(f"output already exists: {output}")
    if not texconv.is_file():
        raise FileNotFoundError(f"texconv is missing: {texconv}")
    output.parent.mkdir(parents=True, exist_ok=True)
    materials = scan_layers(layers)
    if not materials:
        raise ValueError("no _s.dds or _m.dds source maps were found")
    print(
        f"authored-pbr: indexed {len(materials)} effective materials "
        f"({sum(item.metallic_mask_enabled for item in materials)} metal masks)",
        flush=True,
    )

    build_root = Path(
        tempfile.mkdtemp(prefix=f".{output.name}.building-", dir=output.parent)
    )
    try:
        with tempfile.TemporaryDirectory(
            prefix="fo4vr-authored-pbr-", dir=output.parent
        ) as temporary:
            intermediate_root = Path(temporary)
            generated: list[tuple[MaterialInput, Path, tuple[int, int]]] = []
            with concurrent.futures.ThreadPoolExecutor(
                max_workers=max(1, workers)
            ) as executor:
                for index, result in enumerate(
                    executor.map(
                        lambda item: _write_intermediate(item, intermediate_root),
                        materials,
                    ),
                    start=1,
                ):
                    generated.append(result)
                    if index % 256 == 0 or index == len(materials):
                        print(
                            f"authored-pbr: composed {index}/{len(materials)} maps",
                            flush=True,
                        )

            directories: dict[Path, Path] = {}
            for material, intermediate, _ in generated:
                relative_parent = _relative_rmaos_path(
                    material.rmaos_path
                ).parent
                directories[intermediate.parent] = build_root / relative_parent
            ordered_directories = sorted(
                directories.items(), key=lambda item: str(item[0]).lower()
            )
            for index, (source_directory, destination_directory) in enumerate(
                ordered_directories, start=1
            ):
                _run_texconv(texconv, source_directory, destination_directory)
                if index % 64 == 0 or index == len(ordered_directories):
                    print(
                        f"authored-pbr: compressed {index}/{len(ordered_directories)} directories",
                        flush=True,
                    )

            outputs: list[dict[str, object]] = []
            for index, (material, intermediate, expected_size) in enumerate(
                generated, start=1
            ):
                destination_directory = build_root / _relative_rmaos_path(
                    material.rmaos_path
                ).parent
                final = _find_texconv_output(
                    destination_directory, intermediate.stem
                )
                metadata = read_dds_metadata(final)
                expected_mips = math.floor(math.log2(max(expected_size))) + 1
                if (
                    (metadata.width, metadata.height) != expected_size
                    or metadata.mip_count != expected_mips
                    or metadata.dxgi_format != 98
                ):
                    raise ValueError(
                        f"invalid BC7/mip contract for {final}: {metadata}"
                    )
                outputs.append(
                    {
                        "path": material.rmaos_path,
                        "size": final.stat().st_size,
                        "sha256": sha256(final),
                    }
                )
                if index % 256 == 0 or index == len(generated):
                    print(
                        f"authored-pbr: validated {index}/{len(generated)} DDS files",
                        flush=True,
                    )

        runtime_manifest = {
            "materials": [
                {"base": material.base_path, "rmaos": material.rmaos_path}
                for material in materials
            ]
        }
        manifest_path = (
            build_root
            / "F4SE"
            / "Plugins"
            / "FO4VRCommunityShaders"
            / "PBRMaterials"
            / "vanilla-vivid-textures.json"
        )
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(
            json.dumps(runtime_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )

        archive_records = []
        for name, path in source_archives:
            if not path.is_file():
                raise FileNotFoundError(f"source archive is missing: {path}")
            archive_records.append(
                {
                    "name": name,
                    "path": str(path),
                    "size": path.stat().st_size,
                    "sha256": sha256(path),
                }
            )
            print(f"authored-pbr: hashed source archive {name}", flush=True)
        report: dict[str, object] = {
            "schema": 1,
            "conversion": {
                "roughness": "1 - legacy _s.G, clamped to 10/255 minimum",
                "metalness": "legacy _m.R only for high-confidence metal paths",
                "ambientOcclusion": "1.0 neutral (no source AO)",
                "dielectricF0": "legacy _s.R remapped to 0.02..0.08",
                "format": "BC7_UNORM with complete mip chain",
            },
            "precedenceLowToHigh": [layer.name for layer in layers],
            "layers": [
                {"name": layer.name, "root": str(layer.root)} for layer in layers
            ],
            "sourceArchives": archive_records,
            "materialCount": len(materials),
            "metallicMaterialCount": sum(
                material.metallic_mask_enabled for material in materials
            ),
            "inferredBasePaths": True,
            "outputs": sorted(outputs, key=lambda item: str(item["path"])),
            "runtimeManifest": str(manifest_path.relative_to(build_root)),
        }
        report_path = build_root / "FO4VRCommunityShaders_AuthoredPBR_build.json"
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        build_root.replace(output)
        return report
    except Exception:
        shutil.rmtree(build_root, ignore_errors=True)
        raise


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--layer",
        action="append",
        required=True,
        type=parse_named_path,
        metavar="NAME=PATH",
        help="loose staging layer, repeated from lowest to highest precedence",
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--texconv", required=True, type=Path)
    parser.add_argument(
        "--source-archive",
        action="append",
        default=[],
        type=parse_named_path,
        metavar="NAME=PATH",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=min(8, os.cpu_count() or 1),
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = make_parser().parse_args(argv)
    layers = [Layer(name, path) for name, path in arguments.layer]
    report = build_pack(
        layers=layers,
        output=arguments.output.resolve(),
        texconv=arguments.texconv.resolve(),
        source_archives=arguments.source_archive,
        workers=arguments.workers,
    )
    print(
        json.dumps(
            {
                "output": str(arguments.output.resolve()),
                "materials": report["materialCount"],
                "metallicMaterials": report["metallicMaterialCount"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
