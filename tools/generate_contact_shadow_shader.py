from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census
from dxbc_transform import (
    OPCODE_DCL_CONSTANT_BUFFER,
    OPCODE_DCL_RESOURCE,
    OPCODE_RET,
    OPERAND_CONSTANT_BUFFER,
    OPERAND_OUTPUT,
    OPERAND_RESOURCE,
    OPERAND_TEMP,
    DxbcChunk,
    TransformError as ContractError,
    build_dxbc,
    executable_operands,
    instructions,
    pack_words,
    replace_operand_with_temp,
    shader_declarations_and_body,
    shader_words,
)


EXPECTED_IDENTITY = (26152, "12280787d2a5110c820f433751c84648")
EXPECTED_ALIAS_KEYS = {0x01200202, 0x01200282, 0x11200202}
CONTACT_CONSTANT_SLOT = 13
CONTACT_MASK_SLOT = 46
MUL_OPCODE = 0x38
DIV_OPCODE = 0x0E


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the exact FO4VR directional contact-shadow shader."
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--compute-binary", type=Path, required=True)
    parser.add_argument("--compute-header", type=Path, required=True)
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
        raise ContractError(
            f"{label} failed: {(result.stdout + result.stderr).strip()}"
        )


def original_shader(root: Path) -> bytes:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    matches = [
        item
        for item in inventory.containers
        if item.family == "DFLight"
        and item.stage == "PS"
        and item.identity == EXPECTED_IDENTITY
    ]
    keys = {int(item.key) for item in matches if item.key is not None}
    if keys != EXPECTED_ALIAS_KEYS or len(matches) != len(EXPECTED_ALIAS_KEYS):
        raise ContractError(
            "exact FO4VR directional DFLight aliases changed: "
            f"expected {sorted(EXPECTED_ALIAS_KEYS)}, found {sorted(keys)}"
        )
    return matches[0].data


def compile_template(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowTransform.hlsl"
    )
    output = temporary / "ContactShadowTransform.dxbc"
    assembly = temporary / "ContactShadowTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB13[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t46",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow transform assembly changed: " + required
            )
    return output.read_bytes()


def replace_instruction_operands(
    instruction: list[int], replacements: dict[int, list[int]]
) -> list[int]:
    operands = {
        operand.start: operand
        for operand in executable_operands(instruction, 0, len(instruction))
    }
    result: list[int] = []
    cursor = 0
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is None:
            result.append(instruction[cursor])
            cursor += 1
            continue
        operand = operands.get(cursor)
        if operand is None:
            raise ContractError("contact-shadow operand replacement is invalid")
        result.extend(replacement)
        cursor = operand.end
    result[0] = (result[0] & ~(0x7F << 24)) | (len(result) << 24)
    return result


def template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    resource_declaration: list[int] | None = None
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0] == CONTACT_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (CONTACT_MASK_SLOT,)
        ):
            resource_declaration = words[start:end]
    if constant_declaration is None:
        raise ContractError("contact-shadow template no longer declares b13")
    if resource_declaration is None:
        raise ContractError("contact-shadow template no longer declares t46")

    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("contact-shadow template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError(
            "contact-shadow template contains an early return that would exit DFLight"
        )
    return [resource_declaration, constant_declaration], transform[:-1], temp_count


def remap_transform_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int,
    visibility_scratch: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("contact-shadow template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("contact-shadow template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                visibility_scratch,
            )
    return replace_instruction_operands(instruction, replacements)


def temp_mask(register: int, mask: int) -> list[int]:
    return [0x00100002 | (mask << 4), register]


def temp_swizzle(register: int, swizzle: int) -> list[int]:
    return [0x00100000 | swizzle, register]


def temp_scalar(register: int, component: int = 0) -> list[int]:
    return [0x0010000A | (component << 4), register]


def multiply_rgb(register: int, visibility_scratch: int) -> list[int]:
    result = [
        MUL_OPCODE,
        *temp_mask(register, 0x7),
        *temp_swizzle(register, 0x246),
        *temp_scalar(visibility_scratch),
    ]
    result[0] |= len(result) << 24
    return result


def output_contract(
    words: list[int], body: list[tuple[int, int]], register: int, opcode: int
) -> tuple[int, int]:
    matches: list[tuple[int, int]] = []
    for start, end in body:
        if (words[start] & 0x7FF) != opcode:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == OPERAND_OUTPUT
            and operands[0].immediate_indices == (register,)
        ):
            matches.append((start, end))
    if len(matches) != 1:
        raise ContractError(
            f"directional DFLight output o{register} contract changed"
        )
    return matches[0]


def patch_shader(original: bytes, template: bytes) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temps = words[temp_declaration[0] + 1]
    if original_temps != 11:
        raise ContractError(
            f"directional DFLight temporary count changed: {original_temps}"
        )
    declarations, transform, template_temps = template_contract(template)
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != OPCODE_DCL_RESOURCE:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (CONTACT_MASK_SLOT,)
        ):
            raise ContractError("directional DFLight unexpectedly owns t46")
    first_scratch = original_temps
    visibility_scratch = first_scratch + template_temps

    o1_write = output_contract(words, body, 1, MUL_OPCODE)
    o0_write = output_contract(words, body, 0, DIV_OPCODE)
    transformed: list[int] = []
    for instruction in transform:
        transformed.extend(
            remap_transform_instruction(
                instruction,
                first_scratch,
                template_temps,
                visibility_scratch,
            )
        )

    prefix = words[: temp_declaration[0]]
    for declaration in declarations:
        prefix.extend(declaration)
    updated_temps = words[temp_declaration[0] : temp_declaration[1]]
    updated_temps[1] = visibility_scratch + 1
    rewritten: list[int] = []
    for start, end in body:
        if (start, end) == o1_write:
            rewritten.extend(transformed)
            rewritten.extend(multiply_rgb(1, visibility_scratch))
        elif (start, end) == o0_write:
            rewritten.extend(multiply_rgb(0, visibility_scratch))
        rewritten.extend(words[start:end])

    patched_words = [*prefix, *updated_temps, *rewritten]
    patched_words[1] = len(patched_words)
    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def compile_compute_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowMaskCS.hlsl"
    )
    output = temporary / "ContactShadowMaskCS.dxbc"
    assembly = temporary / "ContactShadowMaskCS.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "cs_5_0",
            "/E",
            "CSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow mask compute compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_uav_typed_texture2d (unorm,unorm,unorm,unorm) u0",
        "dcl_constantbuffer CB2[46], dynamicIndexed",
        "dcl_constantbuffer CB8[1], immediateIndexed",
        "dcl_constantbuffer CB12[48], dynamicIndexed",
        "dcl_constantbuffer CB13[3], immediateIndexed",
        "dcl_thread_group 8, 8, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow mask compute assembly changed: " + required
            )
    return output.read_bytes()


def write_header(path: Path, data: bytes, symbol: str) -> None:
    rows = [
        "#pragma once",
        "",
        f"inline constexpr unsigned char {symbol}[] = {{",
    ]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        rows.append(f"    {values},")
    rows.extend(("};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def main() -> int:
    args = arguments()
    root = args.root.resolve()
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.compute_binary.parent.mkdir(parents=True, exist_ok=True)
    args.compute_header.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fo4vr-contact-shadows-") as folder:
        temporary = Path(folder)
        original = original_shader(root)
        template = compile_template(root, args.fxc.resolve(), temporary)
        compute = compile_compute_shader(root, args.fxc.resolve(), temporary)
        candidate = patch_shader(original, template)
        candidate_path = temporary / "ContactShadowsDFLight.dxbc"
        assembly_path = temporary / "ContactShadowsDFLight.asm.txt"
        candidate_path.write_bytes(candidate)
        run(
            [
                str(args.fxc.resolve()),
                "/dumpbin",
                "/nologo",
                "/Fc",
                str(assembly_path),
                str(candidate_path),
            ],
            "contact-shadow candidate validation",
        )
        assembly = assembly_path.read_text(encoding="utf-8")
        for required in (
            "dcl_constantbuffer CB13[1], immediateIndexed",
            "dcl_resource_texture2d (float,float,float,float) t46",
            "dcl_output o0.xyzw",
            "dcl_output o1.xyzw",
        ):
            if required not in assembly:
                raise ContractError(
                    "contact-shadow candidate validation failed: " + required
                )
        if sum(line.strip() == "ret" for line in assembly.splitlines()) != 1:
            raise ContractError(
                "contact-shadow candidate contains an injected early return"
            )
        args.binary.write_bytes(candidate)
        write_header(
            args.header,
            candidate,
            "fo4vr_cs_contact_shadows_dflight",
        )
        args.compute_binary.write_bytes(compute)
        write_header(
            args.compute_header,
            compute,
            "fo4vr_cs_contact_shadow_mask",
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError) as error:
        print(f"contact-shadow generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
