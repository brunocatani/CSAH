"""
disasm_dxbc.py — Disassemble a DXBC PS to readable SM5 assembly.
Parses ISGN, OSGN, SHEX/SHDR chunks and prints readable disassembly.
Focus: identify which input registers carry UVs, normals, tangents, vertex colors.
"""

import struct
import sys
from pathlib import Path

def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]

def f32(data, off):
    return struct.unpack_from("<f", data, off)[0]

def read_cstring(data, off):
    end = off
    while end < len(data) and data[end] != 0:
        end += 1
    return data[off:end].decode("ascii", errors="replace")

# SM5 opcode table (subset)
OPCODES = {
    0x00: "add", 0x01: "and", 0x02: "break", 0x03: "breakc",
    0x04: "call", 0x05: "callc", 0x06: "case", 0x07: "continue",
    0x08: "continuec", 0x09: "cut", 0x0A: "default", 0x0B: "deriv_rtx",
    0x0C: "deriv_rty", 0x0D: "discard", 0x0E: "div", 0x0F: "dp2",
    0x10: "dp3", 0x11: "dp4", 0x12: "else", 0x13: "emit",
    0x14: "emitthencut", 0x15: "endif", 0x16: "endloop", 0x17: "endswitch",
    0x18: "eq", 0x19: "exp", 0x1A: "frc", 0x1B: "ftoi",
    0x1C: "ftou", 0x1D: "ge", 0x1E: "iadd", 0x1F: "if",
    0x20: "ieq", 0x21: "ige", 0x22: "ilt", 0x23: "imad",
    0x24: "imax", 0x25: "imin", 0x26: "imul", 0x27: "ine",
    0x28: "ineg", 0x29: "ishl", 0x2A: "ishr", 0x2B: "itof",
    0x2C: "label", 0x2D: "ld", 0x2E: "ld_ms", 0x2F: "log",
    0x30: "loop", 0x31: "lt", 0x32: "mad", 0x33: "min",
    0x34: "max", 0x36: "mov", 0x37: "movc", 0x38: "mul",
    0x39: "ne", 0x3A: "nop", 0x3B: "not", 0x3C: "or",
    0x3D: "resinfo", 0x3E: "ret", 0x3F: "retc",
    0x40: "round_ne", 0x41: "round_ni", 0x42: "round_pi", 0x43: "round_z",
    0x44: "rsq", 0x45: "sample", 0x46: "sample_c", 0x47: "sample_c_lz",
    0x48: "sample_l", 0x49: "sample_d", 0x4A: "sample_b",
    0x4E: "sqrt",
    0x50: "switch", 0x51: "sincos",
    0x55: "ushr", 0x56: "utof", 0x57: "xor",
    # Declaration opcodes
    0x59: "dcl_constant_buffer", 0x5A: "dcl_sampler",
    0x5B: "dcl_resource", 0x5C: "dcl_input", 0x5D: "dcl_input_sgv",
    0x5E: "dcl_input_siv", 0x5F: "dcl_input_ps",
    0x60: "dcl_input_ps_sgv", 0x61: "dcl_input_ps_siv",
    0x62: "dcl_output", 0x63: "dcl_output_sgv", 0x64: "dcl_output_siv",
    0x65: "dcl_temps", 0x66: "dcl_indexable_temp",
    0x67: "dcl_global_flags",
    0x68: "dcl_hs_max_tessfactor",
    # SM5 extended
    0xC9: "deriv_rtx_coarse", 0xCA: "deriv_rtx_fine",
    0xCB: "deriv_rty_coarse", 0xCC: "deriv_rty_fine",
    0xD9: "sample_pos",
}

COMP = "xyzw"
INTERP_MODES = {0: "", 1: "constant", 2: "linear", 3: "linear_centroid",
                4: "linear_noperspective", 5: "linear_noperspective_centroid",
                6: "linear_sample", 7: "linear_noperspective_sample"}

def swizzle_str(swiz4):
    """Convert 4-component swizzle (0-3 per component) to string."""
    return "".join(COMP[s] for s in swiz4)

def mask_str(mask4):
    """Convert 4-bit mask to .xyzw string."""
    s = "".join(COMP[i] for i in range(4) if mask4 & (1 << i))
    return f".{s}" if s else ""

def decode_operand(data, off, end):
    """Decode a SM5 operand token. Returns (string, new_offset)."""
    if off >= end:
        return "???", off

    token = u32(data, off)
    off += 4

    # Operand type (bits 12-19)
    op_type = (token >> 12) & 0xFF
    # Number of components (bits 0-1)
    num_comp = token & 3
    # Component selection mode (bits 2-3)
    sel_mode = (token >> 2) & 3
    # Index dimension (bits 20-21)
    idx_dim = (token >> 20) & 3

    # Extended operand?
    extended = (token >> 31) & 1
    ext_token = 0
    if extended:
        if off < end:
            ext_token = u32(data, off)
            off += 4

    # Component string
    comp_str = ""
    if num_comp == 2:  # 4-component
        if sel_mode == 0:  # mask
            mask4 = (token >> 4) & 0xF
            comp_str = mask_str(mask4)
        elif sel_mode == 1:  # swizzle
            swiz = [(token >> (4 + i*2)) & 3 for i in range(4)]
            s = swizzle_str(swiz)
            if s != "xyzw":
                comp_str = f".{s}"
        elif sel_mode == 2:  # select_1
            comp_str = f".{COMP[(token >> 4) & 3]}"
    elif num_comp == 1:  # 1-component
        comp_str = ""  # scalar

    # Type names
    TYPE_NAMES = {
        0: "r", 1: "v", 2: "o", 3: "x", 4: "vIdx", 5: "vPri",
        6: "icb", 7: "ramp_out", 8: "immcb", 9: "sampler",
        10: "resource", 13: "null",
    }

    # Immediate values
    if op_type == 4:  # imm32
        if num_comp == 1:
            val = f32(data, off) if off < end else 0
            off += 4
            if val == int(val):
                return f"l({int(val)})", off
            return f"l({val:.6f})", off
        elif num_comp == 2:
            vals = [f32(data, off + i*4) for i in range(4) if off + i*4 < end]
            off += 16
            strs = []
            for v in vals:
                if v == int(v):
                    strs.append(str(int(v)))
                else:
                    strs.append(f"{v:.6f}")
            return f"l({', '.join(strs)})", off
        return f"imm32", off

    name = TYPE_NAMES.get(op_type, f"type{op_type}")

    # Index
    idx_str = ""
    for d in range(idx_dim):
        idx_repr = (token >> (22 + d*3)) & 7
        if idx_repr == 0:  # immediate 32
            if off < end:
                idx = u32(data, off)
                off += 4
                idx_str += f"{idx}"
            else:
                idx_str += "?"
        elif idx_repr == 1:  # immediate 64
            off += 8
            idx_str += "?"
        elif idx_repr == 2:  # relative
            rel_str, off = decode_operand(data, off, end)
            idx_str += f"[{rel_str}]"
        elif idx_repr == 3:  # imm32 + relative
            if off < end:
                base = u32(data, off)
                off += 4
                rel_str, off = decode_operand(data, off, end)
                idx_str += f"{base}[{rel_str}]"
        if d == 0 and idx_dim > 1:
            idx_str += "["

    # Negate/abs from extended token
    prefix = ""
    suffix = ""
    if extended and ext_token:
        if (ext_token >> 6) & 1:  # negate
            prefix = "-"
        if (ext_token >> 7) & 1:  # abs
            prefix += "|"
            suffix = "|"

    return f"{prefix}{name}{idx_str}{comp_str}{suffix}", off

def disassemble_shex(data, chunk_abs, chunk_size, isgn_map=None):
    """Disassemble SHEX/SHDR bytecode."""
    lines = []

    # Header: version + length
    version_token = u32(data, chunk_abs + 8)
    length_dwords = u32(data, chunk_abs + 12)

    prog_type = (version_token >> 16) & 0xFFFF
    major = (version_token >> 4) & 0xF
    minor = version_token & 0xF

    type_names = {0: "ps", 1: "vs", 2: "gs", 3: "hs", 4: "ds", 5: "cs"}
    lines.append(f"{type_names.get(prog_type, '??')}_{major}_{minor}")
    lines.append("")

    off = chunk_abs + 16  # skip chunk tag(4) + size(4) + version(4) + length(4)
    end = chunk_abs + 8 + length_dwords * 4

    while off < end:
        inst_off = off
        token = u32(data, off)

        opcode = token & 0x7FF
        length = (token >> 24) & 0x7F
        if length == 0:
            # Extended: next token has length
            if off + 4 < end:
                ext = u32(data, off + 4)
                length = ext & 0x3FFFFFFF
                if length == 0:
                    break
            else:
                break

        inst_end = off + length * 4
        op_name = OPCODES.get(opcode, f"op_{opcode:#x}")

        # Special handling for declarations
        if opcode == 0x5F:  # dcl_input_ps
            interp = (token >> 11) & 0xF
            interp_str = INTERP_MODES.get(interp, f"interp{interp}")
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            semantic = ""
            # Try to match with ISGN
            if isgn_map:
                # Extract register number
                for reg_num, sem_name in isgn_map.items():
                    if f"v{reg_num}" in operand:
                        semantic = f"  // {sem_name}"
                        break
            lines.append(f"dcl_input_ps {interp_str} {operand}{semantic}")
            off = inst_end
            continue

        if opcode == 0x61:  # dcl_input_ps_siv
            interp = (token >> 11) & 0xF
            interp_str = INTERP_MODES.get(interp, f"interp{interp}")
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            # SIV name
            siv = u32(data, off) if off < inst_end else 0
            off += 4
            siv_names = {1: "position", 3: "render_target_array_index", 4: "viewport_array_index",
                        6: "instance_id", 7: "primitive_id", 8: "is_front_face",
                        10: "clip_distance", 11: "cull_distance", 12: "target", 13: "depth",
                        14: "coverage", 23: "stencil_ref"}
            siv_name = siv_names.get(siv, f"siv_{siv}")
            lines.append(f"dcl_input_ps_siv {interp_str} {operand}, {siv_name}")
            off = inst_end
            continue

        if opcode == 0x60:  # dcl_input_ps_sgv
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            sgv = u32(data, off) if off < inst_end else 0
            off += 4
            sgv_names = {8: "is_front_face"}
            sgv_name = sgv_names.get(sgv, f"sgv_{sgv}")
            lines.append(f"dcl_input_ps_sgv {operand}, {sgv_name}")
            off = inst_end
            continue

        if opcode == 0x65:  # dcl_temps
            off += 4
            num_temps = u32(data, off) if off < inst_end else 0
            lines.append(f"dcl_temps {num_temps}")
            off = inst_end
            continue

        if opcode == 0x62:  # dcl_output
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            lines.append(f"dcl_output {operand}")
            off = inst_end
            continue

        if opcode == 0x64:  # dcl_output_siv
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            siv = u32(data, off) if off < inst_end else 0
            off += 4
            siv_names = {12: "target", 13: "depth", 14: "depth_ge", 15: "depth_le"}
            siv_name = siv_names.get(siv, f"siv_{siv}")
            lines.append(f"dcl_output_siv {operand}, {siv_name}")
            off = inst_end
            continue

        if opcode == 0x59:  # dcl_constant_buffer
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            lines.append(f"dcl_constantbuffer {operand}")
            off = inst_end
            continue

        if opcode == 0x5A:  # dcl_sampler
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            mode = (token >> 11) & 0xF
            mode_str = {0: "default", 1: "comparison"}.get(mode, f"mode{mode}")
            lines.append(f"dcl_sampler {operand}, {mode_str}")
            off = inst_end
            continue

        if opcode == 0x5B:  # dcl_resource
            off += 4
            operand, off = decode_operand(data, off, inst_end)
            dim = (token >> 11) & 0x1F
            dim_names = {1: "buffer", 2: "texture1d", 3: "texture2d", 4: "texture2dms",
                        5: "texture3d", 6: "texturecube", 7: "texture1darray",
                        8: "texture2darray", 9: "texture2dmsarray", 10: "texturecubearray"}
            dim_name = dim_names.get(dim, f"dim{dim}")
            # Return type token
            ret = u32(data, off) if off < inst_end else 0
            off += 4
            lines.append(f"dcl_resource_{dim_name} (float,float,float,float) {operand}")
            off = inst_end
            continue

        if opcode == 0x67:  # dcl_global_flags
            flags = (token >> 11) & 0x1FF
            flag_strs = []
            if flags & 1: flag_strs.append("refactoringAllowed")
            if flags & 2: flag_strs.append("enableDoublePrecision")
            if flags & 4: flag_strs.append("forceEarlyDepthStencil")
            if flags & 8: flag_strs.append("enableRawAndStructuredBuffers")
            lines.append(f"dcl_globalFlags {' | '.join(flag_strs) if flag_strs else hex(flags)}")
            off = inst_end
            continue

        # Regular instructions: decode operands
        off += 4
        saturate = (token >> 13) & 1
        sat_str = "_sat" if saturate else ""

        operands = []
        safety = 0
        while off < inst_end and safety < 8:
            op_str, off = decode_operand(data, off, inst_end)
            operands.append(op_str)
            safety += 1

        op_str = f"{op_name}{sat_str} {', '.join(operands)}"

        # Annotate sample instructions with texture info
        if "sample" in op_name and len(operands) >= 3:
            op_str += f"  // UV from {operands[1]}"

        lines.append(op_str)
        off = inst_end

    return lines

def parse_isgn(data, chunk_abs):
    """Parse ISGN chunk, return dict of register -> semantic string and element list."""
    reg_map = {}
    elements = []
    chunk_data_start = chunk_abs + 8
    num_elems = u32(data, chunk_abs + 8)
    if num_elems > 64:
        return reg_map, elements
    elem_base = chunk_data_start + 8
    for i in range(num_elems):
        eoff = elem_base + i * 24
        if eoff + 24 > len(data):
            break
        name_rel  = u32(data, eoff)
        sem_idx   = u32(data, eoff + 4)
        sv_type   = u32(data, eoff + 8)
        comp_type = u32(data, eoff + 12)
        reg       = u32(data, eoff + 16)
        mask      = data[eoff + 20]
        name_abs  = chunk_data_start + name_rel
        name      = read_cstring(data, name_abs) if name_abs < len(data) else ""

        sem_str = f"{name}{sem_idx}" if sem_idx > 0 else name
        if sv_type == 1:
            sem_str += " [SV_POS]"
        reg_map[reg] = reg_map.get(reg, "") + ("+" if reg in reg_map else "") + sem_str

        COMP = "xyzw"
        mask_s = "".join(COMP[j] for j in range(4) if mask & (1 << j))
        elements.append((reg, name, sem_idx, sv_type, comp_type, mask_s))

    return reg_map, elements

def parse_osgn(data, chunk_abs):
    """Parse OSGN chunk."""
    elements = []
    chunk_data_start = chunk_abs + 8
    num_elems = u32(data, chunk_abs + 8)
    if num_elems > 64:
        return elements
    elem_base = chunk_data_start + 8
    for i in range(num_elems):
        eoff = elem_base + i * 24
        if eoff + 24 > len(data):
            break
        name_rel  = u32(data, eoff)
        sem_idx   = u32(data, eoff + 4)
        sv_type   = u32(data, eoff + 8)
        comp_type = u32(data, eoff + 12)
        reg       = u32(data, eoff + 16)
        mask      = data[eoff + 20]
        name_abs  = chunk_data_start + name_rel
        name      = read_cstring(data, name_abs) if name_abs < len(data) else ""

        COMP = "xyzw"
        mask_s = "".join(COMP[j] for j in range(4) if mask & (1 << j))
        elements.append((reg, name, sem_idx, sv_type, comp_type, mask_s))

    return elements

def disassemble_file(path):
    data = Path(path).read_bytes()
    if data[:4] != b"DXBC":
        print(f"ERROR: Not a DXBC file: {path}")
        return

    total_size = u32(data, 24)
    num_chunks = u32(data, 28)

    print(f"DXBC size={total_size} chunks={num_chunks}")
    print()

    isgn_map = {}
    isgn_elems = []
    osgn_elems = []
    shex_abs = None
    shex_size = None

    for ci in range(num_chunks):
        chunk_rel = u32(data, 32 + ci * 4)
        chunk_abs = chunk_rel
        tag = data[chunk_abs:chunk_abs+4].decode("ascii", errors="replace")
        chunk_data_size = u32(data, chunk_abs + 4)

        if tag == "ISGN":
            isgn_map, isgn_elems = parse_isgn(data, chunk_abs)
        elif tag == "OSGN":
            osgn_elems = parse_osgn(data, chunk_abs)
        elif tag in ("SHEX", "SHDR"):
            shex_abs = chunk_abs
            shex_size = chunk_data_size

    # Print ISGN
    print("=== INPUT SIGNATURE (ISGN) ===")
    for reg, name, sem_idx, sv_type, comp_type, mask_s in isgn_elems:
        sv = " [SV_POS]" if sv_type == 1 else ""
        ct = {0:"u32", 1:"i32", 2:"f32", 3:"f64"}.get(comp_type, f"type{comp_type}")
        print(f"  v{reg:2d}.{mask_s:4s}  {name}{sem_idx}{sv}  ({ct})")
    print()

    # Print OSGN
    print("=== OUTPUT SIGNATURE (OSGN) ===")
    for reg, name, sem_idx, sv_type, comp_type, mask_s in osgn_elems:
        print(f"  o{reg:2d}.{mask_s:4s}  {name}{sem_idx}")
    print()

    # Disassemble
    if shex_abs is not None:
        print("=== SHADER BYTECODE ===")
        lines = disassemble_shex(data, shex_abs, shex_size, isgn_map)
        for line in lines:
            print(f"  {line}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python disasm_dxbc.py <file.dxbc>")
        sys.exit(1)
    disassemble_file(sys.argv[1])
