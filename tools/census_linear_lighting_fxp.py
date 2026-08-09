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
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Iterable


DXBC_MAGIC = b"DXBC"
DXBC_CONTAINER_VERSION = 1
FXP_RECORD_MAGIC = 0x11223344
FXP_NULL_RECORD_MAGIC = 0x55667788
FXP_MAX_RECORDS = 1_000_000
FXP_MAX_BLOB_SIZE = 64 * 1024 * 1024
LINEAR_LIGHTING_TARGET_FAMILY = "DFPrepass"
FXP_FAMILIES = (
    "BloodSplatter",
    "DistantTree",
    "Particle",
    "Sky",
    "Effect",
    "Lighting",
    "Utility",
    "Water",
    "DFPrepass",
    "DFLight",
    "DFComposite",
    "FaceCustomization",
)
FXP_COMPUTE_FAMILIES = (
    "DFTiledLighting",
    "DFDecalsCS",
    "MeshCombinerCompute",
    "VertexBufferCopyCS",
    "IndexBufferOffsetCS",
    "MergeInstancedVertexBufferCopyCS",
)
FXP_IMAGE_SPACE_BLOCK_COUNT = 160
FXP_FIXED_BUILTIN_LAYOUT = (
    ("FixedBuiltin[0]", (1, 0, 0, 0, 0)),
    ("FixedBuiltin[1]", (1, 0, 0, 1, 0)),
    ("FixedBuiltin[2]", (1, 0, 0, 0, 0)),
    ("FixedBuiltin[3]", (1, 0, 0, 1, 0)),
    ("FixedBuiltin[4]", (1, 0, 0, 0, 0)),
)
FXP_FINAL_COMPUTE_FAMILIES = (
    "DismemberCS",
    "SkinnedDecalCS",
)
FXP_STAGE_LAYOUT = (
    ("VS", 56),
    ("HS", 56),
    ("DS", 56),
    ("PS", 48),
    ("CS", 56),
)
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
    family: str | None = None
    stage: str | None = None
    key: int | None = None
    record_offset: int | None = None

    @property
    def identity(self) -> tuple[int, str]:
        return self.size, self.checksum


@dataclass(frozen=True)
class FxpSlot:
    family: str
    stage: str
    record_offset: int
    key: int | None
    identity: tuple[int, str] | None

    @property
    def is_null(self) -> bool:
        return self.identity is None


@dataclass(frozen=True)
class FxpInventory:
    containers: tuple[DxbcContainer, ...]
    slots: tuple[FxpSlot, ...]


@dataclass(frozen=True)
class ShaderDeclarations:
    constant_buffers: tuple[tuple[int, int], ...]
    samplers: tuple[int, ...]
    textures: tuple[int, ...]
    inputs: tuple[str, ...]
    outputs: tuple[int, ...]
    global_flags: str

    def is_linear_lighting_gbuffer_shape(self) -> bool:
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


def parse_fxp_block(
    data: bytes,
    cursor: int,
    label: str,
) -> tuple[
    int,
    list[DxbcContainer],
    list[FxpSlot],
    tuple[int, int, int, int, int],
]:
    if cursor + 20 > len(data):
        raise CensusError(
            f"FXP has {len(data) - cursor} trailing bytes that do not form the "
            f"{label} five-stage count table at 0x{cursor:X}"
        )
    counts = struct.unpack_from("<5I", data, cursor)
    cursor += 20

    if any(count > FXP_MAX_RECORDS for count in counts):
        raise CensusError(
            f"{label} count table exceeds the per-stage record limit "
            f"of {FXP_MAX_RECORDS}"
        )
    total_count = sum(counts)
    if total_count > FXP_MAX_RECORDS:
        raise CensusError(
            f"{label} declares {total_count} records, exceeding the "
            f"per-block limit of {FXP_MAX_RECORDS}"
        )
    minimum_record_bytes = total_count * 4
    if minimum_record_bytes > len(data) - cursor:
        raise CensusError(
            f"{label} records require at least {minimum_record_bytes} bytes "
            f"but only {len(data) - cursor} remain"
        )

    containers: list[DxbcContainer] = []
    slots: list[FxpSlot] = []
    for (stage, header_size), count in zip(FXP_STAGE_LAYOUT, counts):
        keys: set[int] = set()
        for record_index in range(count):
            record_offset = cursor
            if cursor + 4 > len(data):
                raise CensusError(
                    f"{label} {stage} record {record_index} marker escapes input "
                    f"at 0x{record_offset:X}"
                )
            magic = read_u32(data, cursor)
            if magic == FXP_NULL_RECORD_MAGIC:
                slots.append(
                    FxpSlot(
                        family=label,
                        stage=stage,
                        record_offset=record_offset,
                        key=None,
                        identity=None,
                    )
                )
                cursor += 4
                continue
            if magic != FXP_RECORD_MAGIC:
                raise CensusError(
                    f"{label} {stage} record {record_index} has magic "
                    f"0x{magic:08X}, expected 0x{FXP_RECORD_MAGIC:08X} or "
                    f"null marker 0x{FXP_NULL_RECORD_MAGIC:08X}, "
                    f"at 0x{record_offset:X}"
                )
            if cursor + header_size > len(data):
                raise CensusError(
                    f"{label} {stage} record {record_index} header escapes input "
                    f"at 0x{record_offset:X}"
                )

            declared_size = read_u32(data, cursor + 4)
            key = read_u32(data, cursor + 8)
            if declared_size > FXP_MAX_BLOB_SIZE:
                raise CensusError(
                    f"{label} {stage} record {record_index} declares "
                    f"{declared_size} bytes, exceeding the blob limit "
                    f"of {FXP_MAX_BLOB_SIZE}"
                )
            if key in keys:
                raise CensusError(
                    f"{label} {stage} has duplicate key 0x{key:08X} "
                    f"at record {record_index}"
                )
            keys.add(key)
            dxbc_offset = cursor + header_size
            container = parse_dxbc(data, dxbc_offset)
            if container.size != declared_size:
                raise CensusError(
                    f"{label} {stage} record {record_index} declares "
                    f"{declared_size} DXBC bytes but its container declares "
                    f"{container.size} at 0x{record_offset:X}"
                )
            if container.shader_type != stage:
                raise CensusError(
                    f"{label} {stage} record {record_index} contains a "
                    f"{container.shader_type} program at 0x{record_offset:X}"
                )

            container = replace(
                container,
                family=label,
                stage=stage,
                key=key,
                record_offset=record_offset,
            )
            containers.append(container)
            slots.append(
                FxpSlot(
                    family=label,
                    stage=stage,
                    record_offset=record_offset,
                    key=key,
                    identity=container.identity,
                )
            )
            cursor = dxbc_offset + declared_size

    return cursor, containers, slots, counts


def parse_fxp(data: bytes) -> FxpInventory:
    containers: list[DxbcContainer] = []
    slots: list[FxpSlot] = []
    cursor = 0

    for family in FXP_FAMILIES:
        cursor, block_containers, block_slots, _ = parse_fxp_block(
            data, cursor, family
        )
        containers.extend(block_containers)
        slots.extend(block_slots)

    for family in FXP_COMPUTE_FAMILIES:
        cursor, block_containers, block_slots, counts = parse_fxp_block(
            data, cursor, family
        )
        if any(counts[:4]):
            raise CensusError(
                f"{family} has nonzero reserved counts {counts[:4]}"
            )
        containers.extend(block_containers)
        slots.extend(block_slots)

    for image_space_index in range(FXP_IMAGE_SPACE_BLOCK_COUNT):
        label = f"ImageSpace[{image_space_index:03d}]"
        cursor, block_containers, block_slots, _ = parse_fxp_block(
            data, cursor, label
        )
        containers.extend(block_containers)
        slots.extend(block_slots)

    for family, expected_counts in FXP_FIXED_BUILTIN_LAYOUT:
        cursor, block_containers, block_slots, counts = parse_fxp_block(
            data, cursor, family
        )
        if counts != expected_counts:
            raise CensusError(
                f"{family} has count table {counts}, expected {expected_counts}"
            )
        containers.extend(block_containers)
        slots.extend(block_slots)

    for family in FXP_FINAL_COMPUTE_FAMILIES:
        cursor, block_containers, block_slots, counts = parse_fxp_block(
            data, cursor, family
        )
        if any(counts[:4]):
            raise CensusError(
                f"{family} has nonzero reserved counts {counts[:4]}"
            )
        containers.extend(block_containers)
        slots.extend(block_slots)

    if cursor != len(data):
        raise CensusError(
            f"FXP has {len(data) - cursor} trailing bytes after "
            f"{FXP_FINAL_COMPUTE_FAMILIES[-1]} at 0x{cursor:X}"
        )

    return FxpInventory(containers=tuple(containers), slots=tuple(slots))


def load_input(path: Path) -> tuple[list[DxbcContainer], str, list[FxpSlot]]:
    if path.is_file():
        data = path.read_bytes()
        if path.suffix.lower() == ".dxbc":
            containers = [parse_dxbc(data, 0, require_exact_size=True)]
            slots: list[FxpSlot] = []
        elif path.suffix.lower() == ".fxp":
            inventory = parse_fxp(data)
            containers = list(inventory.containers)
            slots = list(inventory.slots)
        else:
            containers = scan_fxp(data)
            slots = []
        if not containers:
            raise CensusError(f"no valid DXBC containers found in {path}")
        return containers, hashlib.sha256(data).hexdigest(), slots

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
    return containers, digest.hexdigest(), []


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
    slots: list[FxpSlot],
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

    gbuffer_shape = [
        container
        for container in unique
        if container.shader_type == "PS"
        and declarations[container.identity].is_linear_lighting_gbuffer_shape()
    ]
    family_ownership_decoded = bool(slots)
    if family_ownership_decoded:
        target_occurrences = [
            container
            for container in containers
            if container.family == LINEAR_LIGHTING_TARGET_FAMILY
            and container.stage == "PS"
        ]
        target_by_identity: dict[tuple[int, str], DxbcContainer] = {}
        for container in target_occurrences:
            target_by_identity.setdefault(container.identity, container)
        target_pixel_shaders = sorted(
            target_by_identity.values(),
            key=lambda item: (item.checksum, item.size),
        )
        target_gbuffer_shaders = [
            container
            for container in target_pixel_shaders
            if declarations[container.identity].is_linear_lighting_gbuffer_shape()
        ]
    else:
        target_occurrences = []
        target_pixel_shaders = gbuffer_shape
        target_gbuffer_shaders = gbuffer_shape

    missing_known = sorted(
        name for identity, name in known.items() if identity not in declarations
    )
    target_gbuffer_identities = {
        container.identity for container in target_gbuffer_shaders
    }
    known_outside_shape = sorted(
        name
        for identity, name in known.items()
        if identity not in target_gbuffer_identities
    )
    if missing_known:
        raise CensusError(
            "known witnesses missing from input: " + ", ".join(missing_known)
        )
    if known_outside_shape and not family_ownership_decoded:
        raise CensusError(
            "known witnesses no longer match the Linear Lighting G-buffer shape: "
            + ", ".join(known_outside_shape)
        )

    groups = Counter(
        declaration_key(declarations[item.identity])
        for item in target_gbuffer_shaders
    )
    known_by_group = Counter(
        declaration_key(declarations[item.identity])
        for item in target_gbuffer_shaders
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

    relevant_occurrences = (
        target_occurrences if family_ownership_decoded else containers
    )
    occurrence_counts = Counter(item.identity for item in relevant_occurrences)
    occurrence_offsets: dict[tuple[int, str], list[str]] = {}
    occurrence_records: dict[tuple[int, str], list[dict[str, object]]] = {}
    for item in relevant_occurrences:
        occurrence_offsets.setdefault(item.identity, []).append(f"0x{item.offset:08X}")
        occurrence_records.setdefault(item.identity, []).append(
            {
                "key": item.key,
                "recordOffset": (
                    f"0x{item.record_offset:08X}"
                    if item.record_offset is not None
                    else None
                ),
                "dxbcOffset": f"0x{item.offset:08X}",
            }
        )
    shader_rows = []
    for item in sorted(
        target_gbuffer_shaders,
        key=lambda shader: (shader.checksum, shader.size),
    ):
        shader_rows.append(
            {
                "checksum": item.checksum,
                "size": item.size,
                "occurrences": occurrence_counts[item.identity],
                "offsets": occurrence_offsets[item.identity],
                "records": occurrence_records[item.identity],
                "knownWitness": known.get(item.identity),
                "groupId": group_ids[declaration_key(declarations[item.identity])],
            }
        )

    known_rows = []
    for identity, name in sorted(known.items(), key=lambda item: item[1].lower()):
        identity_occurrences = [
            item for item in containers if item.identity == identity
        ]
        known_rows.append(
            {
                "name": name,
                "size": identity[0],
                "checksum": identity[1],
                "inExactTargetGBufferSet": identity in target_gbuffer_identities,
                "occurrences": [
                    {
                        "family": item.family,
                        "stage": item.stage,
                        "key": item.key,
                        "recordOffset": (
                            f"0x{item.record_offset:08X}"
                            if item.record_offset is not None
                            else None
                        ),
                        "dxbcOffset": f"0x{item.offset:08X}",
                    }
                    for item in identity_occurrences
                ],
            }
        )

    shader_type_counts = Counter(item.shader_type for item in unique)
    family_stage_rows: list[dict[str, object]] = []
    if family_ownership_decoded:
        block_labels = list(dict.fromkeys(slot.family for slot in slots))
        for family in block_labels:
            for stage, _ in FXP_STAGE_LAYOUT:
                family_stage_slots = [
                    slot
                    for slot in slots
                    if slot.family == family and slot.stage == stage
                ]
                family_stage_records = [
                    slot for slot in family_stage_slots if not slot.is_null
                ]
                family_stage_rows.append(
                    {
                        "family": family,
                        "stage": stage,
                        "slots": len(family_stage_slots),
                        "nullSlots": len(family_stage_slots)
                        - len(family_stage_records),
                        "records": len(family_stage_records),
                        "uniqueShaders": len(
                            {slot.identity for slot in family_stage_records}
                        ),
                    }
                )

    target_slots = [
        slot
        for slot in slots
        if slot.family == LINEAR_LIGHTING_TARGET_FAMILY and slot.stage == "PS"
    ]

    return {
        "schemaVersion": 2,
        "source": str(source.resolve()),
        "sourceSha256": source_sha256,
        "fxc": str(fxc),
        "containerCount": len(containers),
        "uniqueContainerCount": len(unique),
        "uniqueShaderTypes": dict(sorted(shader_type_counts.items())),
        "familyOwnershipDecoded": family_ownership_decoded,
        "imageSpaceBlockCount": (
            FXP_IMAGE_SPACE_BLOCK_COUNT if family_ownership_decoded else 0
        ),
        "familyStageCounts": family_stage_rows,
        "gbufferShapePixelShaderCount": len(gbuffer_shape),
        "targetFamily": LINEAR_LIGHTING_TARGET_FAMILY,
        "targetPixelShaderSlots": len(target_slots),
        "targetPixelShaderNullSlots": sum(
            slot.is_null for slot in target_slots
        ),
        "targetPixelShaderOccurrences": len(target_occurrences),
        "targetPixelShaderCount": len(target_pixel_shaders),
        "targetGBufferShapeCount": len(target_gbuffer_shaders),
        "knownWitnessCount": len(known),
        "knownOutsideExactTargetSet": known_outside_shape,
        "knownWitnesses": known_rows,
        "groups": group_rows,
        "targetShaders": shader_rows,
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
    parser.add_argument("--expect-target-pixel", type=int)
    parser.add_argument("--expect-target-shape", type=int)
    parser.add_argument("--expect-known", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        containers, source_sha256, slots = load_input(args.input)
        known = load_known_witnesses(args.known_dir)
        report = build_report(
            args.input,
            source_sha256,
            containers,
            slots,
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
            "Linear Lighting target pixel shader count",
            report["targetPixelShaderCount"],
            args.expect_target_pixel,
        )
        require_count(
            "Linear Lighting target G-buffer shape count",
            report["targetGBufferShapeCount"],
            args.expect_target_shape,
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
            f"target_pixel={report['targetPixelShaderCount']} "
            f"target_shape={report['targetGBufferShapeCount']} "
            f"known={report['knownWitnessCount']}"
        )
        return 0
    except (CensusError, OSError, ValueError) as error:
        print(f"Linear Lighting DXBC census failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
