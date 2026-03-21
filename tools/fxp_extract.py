"""
fxp_extract.py — Extract DXBC shader blobs from a Bethesda FXP shader archive.

FXP Format (reverse-engineered from Shaders011.fxp):
  Each entry = [optional group header] + sentinel record + DXBC blob

  Sentinel record layout:
    +0x00  u32  0x11223344  (magic / entry sentinel)
    +0x04  u32  dxbc_size   (total size of the following DXBC blob)
    +0x08  u32  perm_index  (permutation index within the shader group)
    +0x0C  u32  flag_mask0  (technique flag mask word 0; 0xFF bits = don't-care)
    +0x10  u32  flag_val0   (technique flag value word 0)
    +0x14  ...  (additional flag/constant words, vary by entry size)
    ...
    -0x04  u32  pre_dxbc    (4 bytes immediately before DXBC magic; encode shader-table offset)
  DXBC blob:
    +0x00  4B   'DXBC'      (magic)
    +0x04  16B  checksum
    +0x14  u32  1           (version = 1)
    +0x18  u32  total_size  (== dxbc_size from sentinel record)
    +0x1C  u32  num_chunks
    +0x20  u32[num_chunks]  chunk offsets (relative to DXBC magic)

  Known technique flag bit (bit 11 of flag_val0, when the corresponding mask bit is 0):
    0x0800 = Parallax / POM enabled

Usage:
    py -3 tools/fxp_extract.py [fxp_path] [output_dir]

Defaults:
    fxp_path   = Shaders011.fxp  (relative to repo root, or absolute)
    output_dir = tools/extracted_shaders/
"""

from __future__ import annotations

import os
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DXBC_MAGIC = b"DXBC"
FXP_SENTINEL = 0x11223344

# D3D10_SB_TOKENIZED_PROGRAM_TYPE values encoded in bits 16-31 of the first
# SHEX/SHDR bytecode token.
SHADER_PROG_TYPE: dict[int, str] = {
    0: "PS",
    1: "VS",
    2: "GS",
    3: "HS",
    4: "DS",
    5: "CS",
}

# System-value semantics in ISGN/OSGN elements.
SV_POSITION = 1   # D3D10_SB_NAME_POSITION

# Technique flag bit for parallax occlusion mapping (BSLightingShader bit 11).
POM_BIT = 0x0800


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class SignatureElement:
    name: str
    semantic_index: int
    sv_type: int
    component_type: int
    register: int


@dataclass
class DxbcInfo:
    offset: int          # absolute byte offset inside FXP
    total_size: int
    num_chunks: int
    shader_type: str     # PS / VS / GS / HS / DS / CS / UNK
    has_osgn_sv_pos: bool = False
    has_isgn_sv_pos: bool = False
    osgn_names: list[str] = field(default_factory=list)
    isgn_names: list[str] = field(default_factory=list)
    # Technique flags from the preceding sentinel record
    perm_index: int = 0
    flag_mask0: int = 0xFFFFFFFF
    flag_val0: int = 0xFFFFFFFF
    flag_payload: bytes = b""   # full raw bytes between sentinel+12 and DXBC
    pom_bit_specified: bool = False
    pom_bit_set: bool = False


# ---------------------------------------------------------------------------
# DXBC parsing helpers
# ---------------------------------------------------------------------------

def _read_u32(data: bytes, offset: int) -> int:
    (v,) = struct.unpack_from("<I", data, offset)
    return v


def _read_cstring(data: bytes, abs_offset: int) -> str:
    """Read a null-terminated ASCII string from *data* at *abs_offset*."""
    end = abs_offset
    limit = min(abs_offset + 256, len(data))
    while end < limit and data[end] != 0:
        end += 1
    return data[abs_offset:end].decode("ascii", errors="replace")


def _parse_signature_chunk(data: bytes, chunk_abs: int) -> list[SignatureElement]:
    """
    Parse an ISGN or OSGN chunk and return its signature elements.

    Chunk layout (all relative to chunk_abs):
        +0  4B  tag ('ISGN' or 'OSGN')
        +4  u32 chunk_data_size
        -- chunk data starts at +8 --
        +8  u32 num_elements
        +12 u32 unk  (always 8 in practice)
        +16 element[0..n-1], each 24 bytes:
               +0  u32 name_offset  (relative to chunk_data_start = chunk_abs+8)
               +4  u32 semantic_index
               +8  u32 sv_type
               +12 u32 component_type
               +16 u32 register
               +20 u8  mask
               +21 u8  read_write_mask
               +22 u8  stream
               +23 u8  pad
        then null-terminated name strings
    """
    elements: list[SignatureElement] = []
    try:
        chunk_data_start = chunk_abs + 8   # skip tag + size fields
        num_elems = _read_u32(data, chunk_abs + 8)
        if num_elems > 64:
            return elements
        elem_base = chunk_data_start + 8   # skip n_elems + unk
        for i in range(num_elems):
            eoff = elem_base + i * 24
            if eoff + 24 > len(data):
                break
            name_rel = _read_u32(data, eoff)
            sem_idx   = _read_u32(data, eoff + 4)
            sv_type   = _read_u32(data, eoff + 8)
            comp_type = _read_u32(data, eoff + 12)
            reg       = _read_u32(data, eoff + 16)
            name_abs  = chunk_data_start + name_rel
            name      = _read_cstring(data, name_abs) if name_abs < len(data) else ""
            elements.append(SignatureElement(name, sem_idx, sv_type, comp_type, reg))
    except Exception:
        pass
    return elements


def parse_dxbc(data: bytes, dxbc_abs: int) -> Optional[DxbcInfo]:
    """
    Parse DXBC header and signature chunks starting at *dxbc_abs*.
    Returns None on malformed data.
    """
    try:
        if data[dxbc_abs : dxbc_abs + 4] != DXBC_MAGIC:
            return None
        # DXBC header layout:
        #   +0x00  magic 'DXBC'     (4 B)
        #   +0x04  checksum         (16 B / 4 u32s)
        #   +0x14  one = 0x00000001 (4 B)
        #   +0x18  totalSize        (4 B)  <-- +24
        #   +0x1C  numChunks        (4 B)  <-- +28
        #   +0x20  chunkOffsets[]
        total_size = _read_u32(data, dxbc_abs + 24)   # +0x18
        num_chunks = _read_u32(data, dxbc_abs + 28)   # +0x1C
        if total_size == 0 or total_size > 0x200000 or num_chunks > 32:
            return None
        if dxbc_abs + total_size > len(data):
            return None

        info = DxbcInfo(
            offset=dxbc_abs,
            total_size=total_size,
            num_chunks=num_chunks,
            shader_type="UNK",
        )

        for ci in range(num_chunks):
            chunk_rel_off_addr = dxbc_abs + 32 + ci * 4
            if chunk_rel_off_addr + 4 > len(data):
                break
            chunk_rel = _read_u32(data, chunk_rel_off_addr)
            chunk_abs = dxbc_abs + chunk_rel
            if chunk_abs + 8 > len(data):
                break
            tag = data[chunk_abs : chunk_abs + 4]

            if tag in (b"SHEX", b"SHDR"):
                # First DWORD of bytecode (at chunk_abs+8) encodes shader type in bits 16-31.
                if chunk_abs + 12 <= len(data):
                    first_token = _read_u32(data, chunk_abs + 8)
                    prog_type = (first_token >> 16) & 0xFFFF
                    info.shader_type = SHADER_PROG_TYPE.get(prog_type, f"UNK({prog_type:#06x})")

            elif tag == b"ISGN":
                elems = _parse_signature_chunk(data, chunk_abs)
                info.isgn_names = [e.name for e in elems]
                info.has_isgn_sv_pos = any(e.sv_type == SV_POSITION for e in elems)

            elif tag == b"OSGN":
                elems = _parse_signature_chunk(data, chunk_abs)
                info.osgn_names = [e.name for e in elems]
                info.has_osgn_sv_pos = any(e.sv_type == SV_POSITION for e in elems)

        return info

    except Exception:
        return None


# ---------------------------------------------------------------------------
# FXP scan
# ---------------------------------------------------------------------------

def find_sentinel_before(data: bytes, dxbc_abs: int) -> int:
    """
    Search backwards from *dxbc_abs* for the nearest 0x11223344 sentinel.
    The sentinel is always within 256 bytes of the DXBC magic.
    Returns the sentinel offset, or -1 if not found.
    """
    search_start = max(0, dxbc_abs - 256)
    window = data[search_start:dxbc_abs]
    sent_bytes = struct.pack("<I", FXP_SENTINEL)
    pos = window.rfind(sent_bytes)
    if pos == -1:
        return -1
    return search_start + pos


def decode_technique_flags(data: bytes, sent_abs: int, dxbc_abs: int, info: DxbcInfo) -> None:
    """
    Fill *info* with technique flag information extracted from the sentinel record
    that runs from *sent_abs* to *dxbc_abs*.
    """
    record_len = dxbc_abs - sent_abs
    if record_len < 16:
        return

    info.perm_index = _read_u32(data, sent_abs + 8)
    info.flag_mask0 = _read_u32(data, sent_abs + 12)
    info.flag_val0  = _read_u32(data, sent_abs + 16)

    # Capture full flag payload bytes (sentinel+12 up to but not including DXBC magic).
    payload_start = sent_abs + 12
    payload_end   = dxbc_abs
    info.flag_payload = data[payload_start:payload_end]

    # POM bit (bit 11 / 0x0800) is "specified" when the corresponding mask bit is 0
    # and "set" when the corresponding value bit is 1.
    pom_mask_bit = (info.flag_mask0 >> 11) & 1
    pom_val_bit  = (info.flag_val0  >> 11) & 1
    info.pom_bit_specified = (pom_mask_bit == 0)
    info.pom_bit_set       = (pom_mask_bit == 0) and (pom_val_bit == 1)


def scan_fxp(data: bytes) -> list[DxbcInfo]:
    """Scan *data* for all DXBC blobs and parse each one."""
    results: list[DxbcInfo] = []
    pos = 0
    while True:
        pos = data.find(DXBC_MAGIC, pos)
        if pos == -1:
            break
        info = parse_dxbc(data, pos)
        if info is not None:
            sent_abs = find_sentinel_before(data, pos)
            if sent_abs != -1:
                decode_technique_flags(data, sent_abs, pos, info)
            results.append(info)
            pos += info.total_size  # skip past this blob to avoid false positives inside it
        else:
            pos += 4
    return results


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def _flag_payload_hex(payload: bytes) -> str:
    """Format flag payload as grouped hex string (4 bytes per word)."""
    words = []
    for i in range(0, len(payload), 4):
        chunk = payload[i:i+4]
        if len(chunk) == 4:
            (v,) = struct.unpack("<I", chunk)
            words.append(f"0x{v:08x}")
        else:
            words.append(chunk.hex())
    return "  ".join(words)


def write_meta(meta_path: Path, info: DxbcInfo, index: int) -> None:
    """Write a human-readable sidecar .meta.txt file for one DXBC blob."""
    lines: list[str] = [
        f"# DXBC blob {index:04d}",
        f"offset_in_fxp   = 0x{info.offset:08x}  ({info.offset})",
        f"size_bytes       = 0x{info.total_size:08x}  ({info.total_size})",
        f"num_chunks       = {info.num_chunks}",
        f"shader_type      = {info.shader_type}",
        "",
        "# Signature semantics",
        f"osgn_names       = {info.osgn_names}",
        f"isgn_names       = {info.isgn_names}",
        f"osgn_has_sv_pos  = {info.has_osgn_sv_pos}",
        f"isgn_has_sv_pos  = {info.has_isgn_sv_pos}",
        "",
        "# Technique flags (from preceding FXP sentinel record)",
        f"perm_index       = {info.perm_index}",
        f"flag_mask0       = 0x{info.flag_mask0:08x}",
        f"flag_val0        = 0x{info.flag_val0:08x}",
    ]

    # Annotate individual flag bits where mask0 says they are specified.
    specified_bits: list[str] = []
    for bit in range(32):
        if not ((info.flag_mask0 >> bit) & 1):   # mask bit 0 = specified
            val_bit = (info.flag_val0 >> bit) & 1
            label = ""
            if bit == 0:   label = "VertexColors"
            elif bit == 1:  label = "Skinned"
            elif bit == 2:  label = "LOD"
            elif bit == 3:  label = "AmbientOcclusion"
            elif bit == 5:  label = "ProjectedUV"
            elif bit == 8:  label = "BackLighting"
            elif bit == 9:  label = "EyeEnvMap"
            elif bit == 10: label = "Hair"
            elif bit == 11: label = "Parallax_POM"
            elif bit == 12: label = "MultiLayerParallax"
            desc = f"  bit{bit:2d} (0x{1<<bit:04x}) = {val_bit}"
            if label:
                desc += f"  [{label}]"
            specified_bits.append(desc)
    if specified_bits:
        lines.append("flag_mask0_decoded:")
        lines.extend(specified_bits)

    lines += [
        "",
        f"# POM / Parallax bit (0x{POM_BIT:04x} = bit 11)",
        f"pom_bit_specified = {info.pom_bit_specified}",
        f"pom_bit_set       = {info.pom_bit_set}",
        "",
        "# Full flag payload (sentinel+12 to DXBC start)",
        f"flag_payload_hex  = {_flag_payload_hex(info.flag_payload)}",
    ]

    meta_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    repo_root = Path(__file__).resolve().parent.parent

    fxp_path   = Path(argv[1]) if len(argv) > 1 else repo_root / "Shaders011.fxp"
    output_dir = Path(argv[2]) if len(argv) > 2 else repo_root / "tools" / "extracted_shaders"

    # Resolve relative paths against repo root.
    if not fxp_path.is_absolute():
        fxp_path = repo_root / fxp_path
    if not output_dir.is_absolute():
        output_dir = repo_root / output_dir

    if not fxp_path.exists():
        print(f"ERROR: FXP file not found: {fxp_path}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------------ scan
    fxp_size = fxp_path.stat().st_size
    print(f"Reading {fxp_path.name}  ({fxp_size:,} bytes / {fxp_size/1024/1024:.1f} MB)")
    t0 = time.perf_counter()

    with open(fxp_path, "rb") as fh:
        data = fh.read()

    blobs = scan_fxp(data)
    elapsed = time.perf_counter() - t0
    print(f"Scan complete in {elapsed:.2f}s — {len(blobs)} DXBC blobs found.")

    # --------------------------------------------------------------- extract
    type_counts: dict[str, int] = {}
    pom_blobs: list[tuple[int, DxbcInfo]] = []

    for idx, info in enumerate(blobs):
        type_counts[info.shader_type] = type_counts.get(info.shader_type, 0) + 1

        stem = f"shader_{idx:04d}_{info.shader_type}_0x{info.offset:08x}"
        dxbc_path = output_dir / f"{stem}.dxbc"
        meta_path = output_dir / f"{stem}.meta.txt"

        # Write binary DXBC blob.
        blob_bytes = data[info.offset : info.offset + info.total_size]
        dxbc_path.write_bytes(blob_bytes)

        # Write sidecar metadata.
        write_meta(meta_path, info, idx)

        if info.pom_bit_set:
            pom_blobs.append((idx, info))

    # --------------------------------------------------------------- summary
    print()
    print("=" * 60)
    print(f"  Total DXBC blobs extracted : {len(blobs)}")
    print()
    print("  Breakdown by shader type:")
    for stype in ("VS", "PS", "GS", "HS", "DS", "CS"):
        c = type_counts.get(stype, 0)
        if c:
            print(f"    {stype:4s}  {c:5d}")
    for stype, c in sorted(type_counts.items()):
        if stype not in ("VS", "PS", "GS", "HS", "DS", "CS"):
            print(f"    {stype:10s}  {c:5d}")
    print()
    print(f"  Blobs with POM bit (0x{POM_BIT:04x}) SET : {len(pom_blobs)}")
    if pom_blobs:
        print()
        print("  POM-flagged shaders (first 20):")
        for idx, info in pom_blobs[:20]:
            stem = f"shader_{idx:04d}_{info.shader_type}_0x{info.offset:08x}"
            print(f"    [{idx:4d}] offset=0x{info.offset:08x}  type={info.shader_type:2s}"
                  f"  size={info.total_size:6d}  perm={info.perm_index:4d}"
                  f"  mask0=0x{info.flag_mask0:08x}  val0=0x{info.flag_val0:08x}"
                  f"  -> {stem}.dxbc")
    print()
    print(f"  Output directory: {output_dir}")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
