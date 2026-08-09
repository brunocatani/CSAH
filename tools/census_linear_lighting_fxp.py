from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DXBC_MAGIC = b"DXBC"
DXBC_CONTAINER_VERSION = 1
PROGRAM_TYPES = {
    0: "PS",
    1: "VS",
    2: "GS",
    3: "HS",
    4: "DS",
    5: "CS",
}


class CensusError(RuntimeError):
    pass


@dataclass(frozen=True)
class DxbcContainer:
    offset: int
    size: int
    checksum: str
    shader_type: str
    chunks: tuple[str, ...]
    data: bytes

    @property
    def identity(self) -> tuple[int, str]:
        return self.size, self.checksum


@dataclass(frozen=True)
class ShaderDeclarations:
    constant_buffers: tuple[tuple[int, int], ...]
    samplers: tuple[int, ...]
    textures: tuple[int, ...]
    inputs: tuple[str, ...]
    outputs: tuple[int, ...]
    global_flags: str

    def is_bslighting_gbuffer_shape(self) -> bool:
        buffers = dict(self.constant_buffers)
        return (
            2 in buffers
            and 12 in buffers
            and self.outputs in ((0, 1, 2, 3, 4), (0, 1, 2, 3, 4, 5))
        )


def read_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise CensusError(f"u32 read outside input at 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def parse_dxbc(data: bytes, offset: int, require_exact_size: bool = False) -> DxbcContainer:
    if offset < 0 or offset + 0x20 > len(data):
        raise CensusError(f"DXBC header outside input at 0x{offset:X}")
    if data[offset : offset + 4] != DXBC_MAGIC:
        raise CensusError(f"DXBC magic missing at 0x{offset:X}")

    container_version = read_u32(data, offset + 0x14)
    size = read_u32(data, offset + 0x18)
    chunk_count = read_u32(data, offset + 0x1C)
    header_size = 0x20 + chunk_count * 4
    if container_version != DXBC_CONTAINER_VERSION:
        raise CensusError(
            f"unsupported DXBC container version {container_version} at 0x{offset:X}"
        )
    if chunk_count == 0 or chunk_count > 64:
        raise CensusError(f"invalid DXBC chunk count {chunk_count} at 0x{offset:X}")
    if size < header_size or offset + size > len(data):
        raise CensusError(f"DXBC size escapes input at 0x{offset:X}")
    if require_exact_size and size != len(data) - offset:
        raise CensusError(
            f"DXBC size {size} does not equal file size {len(data) - offset}"
        )

    chunks: list[str] = []
    ranges: list[tuple[int, int]] = []
    shader_type = "UNKNOWN"
    for index in range(chunk_count):
        relative_offset = read_u32(data, offset + 0x20 + index * 4)
        if relative_offset < header_size or relative_offset + 8 > size:
            raise CensusError(
                f"DXBC chunk {index} has invalid offset 0x{relative_offset:X}"
            )
        chunk_offset = offset + relative_offset
        tag_bytes = data[chunk_offset : chunk_offset + 4]
        if any(byte < 0x20 or byte > 0x7E for byte in tag_bytes):
            raise CensusError(f"DXBC chunk {index} has a non-ASCII tag")
        tag = tag_bytes.decode("ascii")
        payload_size = read_u32(data, chunk_offset + 4)
        relative_end = relative_offset + 8 + payload_size
        if relative_end > size:
            raise CensusError(f"DXBC chunk {tag} escapes its container")
        ranges.append((relative_offset, relative_end))
        chunks.append(tag)

        if tag in {"SHDR", "SHEX"}:
            if payload_size < 8:
                raise CensusError(f"DXBC shader chunk {tag} is truncated")
            version_token = read_u32(data, chunk_offset + 8)
            declared_dwords = read_u32(data, chunk_offset + 12)
            if declared_dwords < 2 or declared_dwords * 4 > payload_size:
                raise CensusError(
                    f"DXBC shader chunk {tag} has an invalid token length"
                )
            shader_type = PROGRAM_TYPES.get(
                (version_token >> 16) & 0xFFFF,
                "UNKNOWN",
            )

    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise CensusError("DXBC chunks overlap")
    if shader_type == "UNKNOWN":
        raise CensusError("DXBC container has no recognized shader program")

    blob = data[offset : offset + size]
    return DxbcContainer(
        offset=offset,
        size=size,
        checksum=blob[4:20].hex(),
        shader_type=shader_type,
        chunks=tuple(chunks),
        data=blob,
    )


def scan_fxp(data: bytes) -> list[DxbcContainer]:
    containers: list[DxbcContainer] = []
    cursor = 0
    while True:
        offset = data.find(DXBC_MAGIC, cursor)
        if offset < 0:
            return containers
        try:
            container = parse_dxbc(data, offset)
        except CensusError:
            cursor = offset + 1
            continue
        containers.append(container)
        cursor = offset + container.size


def load_input(path: Path) -> tuple[list[DxbcContainer], str]:
    if path.is_file():
        data = path.read_bytes()
        if path.suffix.lower() == ".dxbc":
            containers = [parse_dxbc(data, 0, require_exact_size=True)]
        else:
            containers = scan_fxp(data)
            if not containers:
                raise CensusError(f"no valid DXBC containers found in {path}")
        return containers, hashlib.sha256(data).hexdigest()

    if not path.is_dir():
        raise CensusError(f"input does not exist: {path}")
    containers: list[DxbcContainer] = []
    digest = hashlib.sha256()
    files = sorted(path.glob("*.dxbc"), key=lambda candidate: candidate.name.lower())
    if not files:
        raise CensusError(f"no .dxbc files found in {path}")
    for file in files:
        data = file.read_bytes()
        digest.update(file.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(data)
        containers.append(parse_dxbc(data, 0, require_exact_size=True))
    return containers, digest.hexdigest()


def find_fxc(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    found = shutil.which("fxc.exe") or shutil.which("fxc")
    if found:
        candidates.append(Path(found))
    candidates.extend(
        [
            Path(
                r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe"
            ),
            Path(
                r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\fxc.exe"
            ),
        ]
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise CensusError("fxc.exe was not found")


def disassemble(fxc: Path, path: Path) -> str:
    completed = subprocess.run(
        [str(fxc), "/dumpbin", "/nologo", str(path)],
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout).strip()
        raise CensusError(f"fxc failed for {path}: {details}")
    if not re.search(r"\b(?:ps|vs|gs|hs|ds|cs)_5_0\b", completed.stdout):
        raise CensusError(f"fxc returned no shader assembly for {path}")
    return completed.stdout


def parse_declarations(assembly: str) -> ShaderDeclarations:
    constant_buffers: dict[int, int] = {}
    samplers: set[int] = set()
    textures: set[int] = set()
    inputs: list[str] = []
    outputs: set[int] = set()
    global_flags = ""

    for raw_line in assembly.splitlines():
        line = raw_line.strip()
        if line.startswith("dcl_globalFlags "):
            global_flags = line.removeprefix("dcl_globalFlags ")
        elif match := re.fullmatch(
            r"dcl_constantbuffer CB(\d+)\[(\d+)\], (?:immediate|dynamic)Indexed",
            line,
        ):
            slot, registers = int(match.group(1)), int(match.group(2))
            if slot in constant_buffers and constant_buffers[slot] != registers:
                raise CensusError(f"conflicting declarations for constant buffer b{slot}")
            constant_buffers[slot] = registers
        elif match := re.match(r"dcl_sampler s(\d+)", line):
            samplers.add(int(match.group(1)))
        elif match := re.match(r"dcl_resource_texture\w+.*\st(\d+)$", line):
            textures.add(int(match.group(1)))
        elif match := re.match(r"dcl_input[^\s]*\s+(.+)", line):
            inputs.append(match.group(1))
        elif match := re.match(r"dcl_output o(\d+)", line):
            outputs.add(int(match.group(1)))

    return ShaderDeclarations(
        constant_buffers=tuple(sorted(constant_buffers.items())),
        samplers=tuple(sorted(samplers)),
        textures=tuple(sorted(textures)),
        inputs=tuple(inputs),
        outputs=tuple(sorted(outputs)),
        global_flags=global_flags,
    )


def unique_containers(containers: Iterable[DxbcContainer]) -> list[DxbcContainer]:
    unique: dict[tuple[int, str], DxbcContainer] = {}
    for container in containers:
        existing = unique.get(container.identity)
        if existing is not None and existing.data != container.data:
            raise CensusError(
                f"DXBC identity collision for size/checksum {container.identity}"
            )
        unique.setdefault(container.identity, container)
    return sorted(unique.values(), key=lambda item: (item.shader_type, item.checksum, item.size))


def load_known_witnesses(path: Path | None) -> dict[tuple[int, str], str]:
    if path is None:
        return {}
    if not path.is_dir():
        raise CensusError(f"known-witness directory does not exist: {path}")
    witnesses: dict[tuple[int, str], str] = {}
    for file in sorted(path.glob("*.dxbc"), key=lambda candidate: candidate.name.lower()):
        container = parse_dxbc(file.read_bytes(), 0, require_exact_size=True)
        if container.identity in witnesses:
            raise CensusError(
                f"duplicate known-witness identity: {file.name} and "
                f"{witnesses[container.identity]}"
            )
        witnesses[container.identity] = file.name
    if not witnesses:
        raise CensusError(f"known-witness directory is empty: {path}")
    return witnesses


def declaration_key(declarations: ShaderDeclarations) -> tuple[object, ...]:
    buffers = dict(declarations.constant_buffers)
    return (
        buffers[2],
        buffers[12],
        len(declarations.outputs),
        declarations.textures,
        declarations.samplers,
        "forceEarlyDepthStencil" in declarations.global_flags,
    )


def build_report(
    source: Path,
    source_sha256: str,
    containers: list[DxbcContainer],
    fxc: Path,
    known: dict[tuple[int, str], str],
) -> dict[str, object]:
    unique = unique_containers(containers)
    source_paths: dict[tuple[int, str], Path] = {}
    if source.is_dir():
        for file in source.glob("*.dxbc"):
            parsed = parse_dxbc(file.read_bytes(), 0, require_exact_size=True)
            source_paths.setdefault(parsed.identity, file)

    declarations: dict[tuple[int, str], ShaderDeclarations] = {}
    with tempfile.TemporaryDirectory(prefix="fo4vr-cs-census-") as temporary:
        temporary_path = Path(temporary)
        for index, container in enumerate(unique):
            shader_path = source_paths.get(container.identity)
            if shader_path is None:
                shader_path = temporary_path / f"shader-{index:04d}.dxbc"
                shader_path.write_bytes(container.data)
            declarations[container.identity] = parse_declarations(
                disassemble(fxc, shader_path)
            )

    bslighting = [
        container
        for container in unique
        if container.shader_type == "PS"
        and declarations[container.identity].is_bslighting_gbuffer_shape()
    ]
    missing_known = sorted(
        name for identity, name in known.items() if identity not in declarations
    )
    known_outside_shape = sorted(
        known[container.identity]
        for container in unique
        if container.identity in known and container not in bslighting
    )
    if missing_known:
        raise CensusError(
            "known witnesses missing from input: " + ", ".join(missing_known)
        )
    if known_outside_shape:
        raise CensusError(
            "known witnesses no longer match the BSLighting G-buffer shape: "
            + ", ".join(known_outside_shape)
        )

    groups = Counter(declaration_key(declarations[item.identity]) for item in bslighting)
    known_by_group = Counter(
        declaration_key(declarations[item.identity])
        for item in bslighting
        if item.identity in known
    )
    sorted_group_keys = sorted(groups, key=lambda item: (-groups[item], item))
    group_ids = {
        key: f"g{index:03d}" for index, key in enumerate(sorted_group_keys)
    }
    group_rows = []
    for key in sorted_group_keys:
        cb2, cb12, mrt_count, textures, samplers, forced_early_depth = key
        group_rows.append(
            {
                "id": group_ids[key],
                "uniqueShaders": groups[key],
                "knownWitnesses": known_by_group[key],
                "cb2Registers": cb2,
                "cb12Registers": cb12,
                "mrtCount": mrt_count,
                "textures": list(textures),
                "samplers": list(samplers),
                "forcedEarlyDepthStencil": forced_early_depth,
            }
        )

    occurrence_counts = Counter(item.identity for item in containers)
    occurrence_offsets: dict[tuple[int, str], list[str]] = {}
    for item in containers:
        occurrence_offsets.setdefault(item.identity, []).append(f"0x{item.offset:08X}")
    shader_rows = []
    for item in sorted(bslighting, key=lambda shader: (shader.checksum, shader.size)):
        shader_rows.append(
            {
                "checksum": item.checksum,
                "size": item.size,
                "occurrences": occurrence_counts[item.identity],
                "offsets": occurrence_offsets[item.identity],
                "knownWitness": known.get(item.identity),
                "groupId": group_ids[declaration_key(declarations[item.identity])],
            }
        )

    shader_type_counts = Counter(item.shader_type for item in unique)
    return {
        "schemaVersion": 1,
        "source": str(source.resolve()),
        "sourceSha256": source_sha256,
        "fxc": str(fxc),
        "containerCount": len(containers),
        "uniqueContainerCount": len(unique),
        "uniqueShaderTypes": dict(sorted(shader_type_counts.items())),
        "bslightingGBufferShapeCount": len(bslighting),
        "knownWitnessCount": len(known),
        "groups": group_rows,
        "bslightingShaders": shader_rows,
    }


def require_count(label: str, actual: int, expected: int | None) -> None:
    if expected is not None and actual != expected:
        raise CensusError(f"{label}: expected {expected}, observed {actual}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Strictly census DXBC containers in a local FO4VR FXP or DXBC directory."
        )
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--known-dir", type=Path)
    parser.add_argument("--fxc", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-total", type=int)
    parser.add_argument("--expect-unique", type=int)
    parser.add_argument("--expect-bslighting-shape", type=int)
    parser.add_argument("--expect-known", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        containers, source_sha256 = load_input(args.input)
        known = load_known_witnesses(args.known_dir)
        report = build_report(
            args.input,
            source_sha256,
            containers,
            find_fxc(args.fxc),
            known,
        )
        require_count("container count", report["containerCount"], args.expect_total)
        require_count(
            "unique container count",
            report["uniqueContainerCount"],
            args.expect_unique,
        )
        require_count(
            "BSLighting G-buffer shape count",
            report["bslightingGBufferShapeCount"],
            args.expect_bslighting_shape,
        )
        require_count(
            "known witness count",
            report["knownWitnessCount"],
            args.expect_known,
        )
        serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(serialized, encoding="utf-8", newline="\n")
        print(
            "Linear Lighting DXBC census verified: "
            f"containers={report['containerCount']} "
            f"unique={report['uniqueContainerCount']} "
            f"bslighting_shape={report['bslightingGBufferShapeCount']} "
            f"known={report['knownWitnessCount']}"
        )
        return 0
    except (CensusError, OSError, ValueError) as error:
        print(f"Linear Lighting DXBC census failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
