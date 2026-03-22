"""
analyze_isgn.py — Analyze PS input signatures (ISGN) from extracted VR DXBC shaders.
Groups PS by their ISGN layout and identifies which layouts POM permutations use.
Also extracts full register mapping (register index, mask, component type) per element.
"""

import struct
import sys
from collections import defaultdict
from pathlib import Path

SV_POSITION = 1

def read_u32(data, off):
    return struct.unpack_from("<I", data, off)[0]

def read_cstring(data, off):
    end = off
    while end < len(data) and data[end] != 0:
        end += 1
    return data[off:end].decode("ascii", errors="replace")

def parse_isgn_full(data, chunk_abs):
    """Parse ISGN chunk, return list of (name, sem_idx, sv_type, comp_type, register, mask)."""
    elements = []
    chunk_data_start = chunk_abs + 8
    num_elems = read_u32(data, chunk_abs + 8)
    if num_elems > 64:
        return elements
    elem_base = chunk_data_start + 8
    for i in range(num_elems):
        eoff = elem_base + i * 24
        if eoff + 24 > len(data):
            break
        name_rel  = read_u32(data, eoff)
        sem_idx   = read_u32(data, eoff + 4)
        sv_type   = read_u32(data, eoff + 8)
        comp_type = read_u32(data, eoff + 12)
        reg       = read_u32(data, eoff + 16)
        mask      = data[eoff + 20] if eoff + 20 < len(data) else 0
        name_abs  = chunk_data_start + name_rel
        name      = read_cstring(data, name_abs) if name_abs < len(data) else ""
        elements.append((name, sem_idx, sv_type, comp_type, reg, mask))
    return elements

def parse_osgn_names(data, chunk_abs):
    """Parse OSGN chunk, return list of (name, sem_idx)."""
    elements = []
    chunk_data_start = chunk_abs + 8
    num_elems = read_u32(data, chunk_abs + 8)
    if num_elems > 64:
        return elements
    elem_base = chunk_data_start + 8
    for i in range(num_elems):
        eoff = elem_base + i * 24
        if eoff + 24 > len(data):
            break
        name_rel = read_u32(data, eoff)
        sem_idx  = read_u32(data, eoff + 4)
        name_abs = chunk_data_start + name_rel
        name     = read_cstring(data, name_abs) if name_abs < len(data) else ""
        elements.append((name, sem_idx))
    return elements

COMP_TYPES = {0: "u32", 1: "i32", 2: "f32", 3: "f64"}
MASK_NAMES = {1: "x", 2: "y", 3: "xy", 4: "z", 5: "xz", 6: "yz", 7: "xyz",
              8: "w", 9: "xw", 0xA: "yw", 0xB: "xyw", 0xC: "zw", 0xD: "xzw",
              0xE: "yzw", 0xF: "xyzw"}

def mask_str(m):
    return MASK_NAMES.get(m, f"0x{m:02x}")

def analyze_dxbc(dxbc_path):
    """Parse a DXBC file and return (isgn_elements, osgn_elements, shader_type)."""
    data = dxbc_path.read_bytes()
    if data[:4] != b"DXBC":
        return None, None, None

    total_size = read_u32(data, 24)
    num_chunks = read_u32(data, 28)

    isgn = None
    osgn = None
    shader_type = "UNK"

    for ci in range(num_chunks):
        chunk_rel = read_u32(data, 32 + ci * 4)
        chunk_abs = chunk_rel
        if chunk_abs + 8 > len(data):
            break
        tag = data[chunk_abs:chunk_abs+4]

        if tag == b"ISGN":
            isgn = parse_isgn_full(data, chunk_abs)
        elif tag == b"OSGN":
            osgn = parse_osgn_names(data, chunk_abs)
        elif tag in (b"SHEX", b"SHDR"):
            first_token = read_u32(data, chunk_abs + 8)
            prog_type = (first_token >> 16) & 0xFFFF
            shader_type = {0:"PS", 1:"VS", 2:"GS", 3:"HS", 4:"DS", 5:"CS"}.get(prog_type, "UNK")

    return isgn, osgn, shader_type

def isgn_key(elements):
    """Create a hashable key from ISGN elements for grouping."""
    return tuple((name, sem_idx, mask) for name, sem_idx, sv_type, comp_type, reg, mask in elements)

def main():
    base = Path(__file__).resolve().parent.parent
    vr_dir = base / "tools" / "extracted_shaders_vr"

    if not vr_dir.exists():
        print(f"ERROR: {vr_dir} not found")
        return 1

    # Find all PS DXBC files
    ps_files = sorted(vr_dir.glob("*_PS_*.dxbc"))
    print(f"Found {len(ps_files)} PS DXBC files")

    # Parse meta files for POM bit
    pom_set = set()  # filenames with POM bit set
    for meta in vr_dir.glob("*_PS_*.meta.txt"):
        text = meta.read_text()
        if "pom_bit_set       = True" in text:
            dxbc_name = meta.name.replace(".meta.txt", ".dxbc")
            pom_set.add(dxbc_name)
    print(f"Found {len(pom_set)} PS with POM bit set")

    # Group by ISGN layout
    groups = defaultdict(list)  # key -> [(filename, isgn_elements, is_pom)]
    errors = 0

    for ps_file in ps_files:
        isgn, osgn, stype = analyze_dxbc(ps_file)
        if isgn is None:
            errors += 1
            continue
        key = isgn_key(isgn)
        is_pom = ps_file.name in pom_set
        groups[key].append((ps_file.name, isgn, is_pom))

    print(f"Parsed {len(ps_files) - errors} PS successfully ({errors} errors)")
    print(f"Found {len(groups)} unique ISGN layouts")
    print()

    # Sort groups by count (most common first)
    sorted_groups = sorted(groups.items(), key=lambda x: -len(x[1]))

    print("=" * 80)
    print("PS INPUT SIGNATURE GROUPS (sorted by frequency)")
    print("=" * 80)

    for rank, (key, members) in enumerate(sorted_groups):
        pom_count = sum(1 for _, _, is_pom in members if is_pom)
        non_pom = len(members) - pom_count

        print(f"\n--- Layout #{rank+1}: {len(members)} shaders ({pom_count} POM, {non_pom} non-POM) ---")

        # Print detailed element info from first member
        _, isgn_elems, _ = members[0]
        for name, sem_idx, sv_type, comp_type, reg, mask in isgn_elems:
            sv_str = " [SV_POSITION]" if sv_type == SV_POSITION else ""
            ct = COMP_TYPES.get(comp_type, f"type{comp_type}")
            print(f"  v{reg:2d}  {name}{sem_idx}{sv_str}  {ct}  mask={mask_str(mask)}")

        # Show a few example filenames
        if pom_count > 0:
            pom_examples = [n for n, _, p in members if p][:3]
            print(f"  POM examples: {', '.join(pom_examples)}")
        non_pom_examples = [n for n, _, p in members if not p][:3]
        if non_pom_examples:
            print(f"  Non-POM examples: {', '.join(non_pom_examples)}")

    # Summary: which layouts contain POM shaders
    print("\n" + "=" * 80)
    print("LAYOUTS CONTAINING POM PERMUTATIONS")
    print("=" * 80)

    pom_layouts = [(rank+1, key, members) for rank, (key, members) in enumerate(sorted_groups)
                   if any(is_pom for _, _, is_pom in members)]

    for layout_num, key, members in pom_layouts:
        pom_count = sum(1 for _, _, p in members if p)
        _, isgn_elems, _ = members[0]
        sem_list = [f"{name}{sem_idx}" for name, sem_idx, *_ in isgn_elems]
        print(f"  Layout #{layout_num}: {pom_count} POM / {len(members)} total  —  {', '.join(sem_list)}")

    # Output the most common POM layout in detail for Lighting.hlsl reconstruction
    if pom_layouts:
        # Find the POM layout with most members
        best_layout_num, best_key, best_members = max(pom_layouts, key=lambda x: sum(1 for _, _, p in x[2] if p))
        _, isgn_elems, _ = best_members[0]
        pom_count = sum(1 for _, _, p in best_members if p)

        print(f"\n{'=' * 80}")
        print(f"RECOMMENDED PS_INPUT (Layout #{best_layout_num}, {pom_count} POM shaders)")
        print(f"{'=' * 80}")
        print()
        print("struct PS_INPUT {")
        for name, sem_idx, sv_type, comp_type, reg, mask in isgn_elems:
            hlsl_type = "float4" if mask == 0xF else f"float{bin(mask).count('1')}" if mask else "float4"
            if sv_type == SV_POSITION:
                sem = "SV_POSITION"
            else:
                sem = f"{name}{sem_idx}" if sem_idx > 0 else name + "0" if name in ("TEXCOORD", "COLOR", "POSITION") else name
            print(f"    {hlsl_type:8s} reg{reg}  : {sem};  // v{reg}, mask={mask_str(mask)}")
        print("};")

        # Also dump the first POM DXBC path for disassembly
        pom_example = next(n for n, _, p in best_members if p)
        print(f"\nFirst POM DXBC for disassembly: {pom_example}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
