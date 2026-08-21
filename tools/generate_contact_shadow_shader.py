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
    OPERAND_INPUT,
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
EXPECTED_COMPATIBLE_IDENTITIES = {
    (6828, "59202691aa359fcd5900f599a5a9401a"),
    (7252, "6df4c07ac73d016d0b078f1bab9bac29"),
    (8064, "636de1da5e378cf72643b3ab1ae84e4f"),
    (8136, "e2774088908877c95b3d85b075cc4c26"),
    (8592, "576f8cd0a7ac65e880e0b467a01ffddc"),
    (9104, "f30e0fb10fefd006db43d4e0b0c8d072"),
    (9344, "8e7b6f32072e8b055d045bf050fc0f29"),
    (9388, "7eb923ee2d544d316d6622e11f1565c0"),
    (9416, "9f68da84fc72a2aacad5e5b904e7543d"),
    (9460, "cf973011206ae3771c81fc2824bd4740"),
    (12444, "89ec8c2ef251d18a6c65bb26fff7f48d"),
    (12516, "5ad3bbc79c0c069a099503309dd79cf8"),
    (23540, "2865b9e659692125a0862a44572f9968"),
    (25356, "954fecb587442ee441be2bbdd3b7b452"),
    (25428, "727d0ca8d00f545ff882644ce9dd810b"),
    (26152, "12280787d2a5110c820f433751c84648"),
    (26224, "ab8a2f400febf79d57d16eacba4742c1"),
    (30248, "cf4a06d7ed1be30eace888f9f49abe10"),
    (30320, "16b391d8e5b12cb2ee54bc649a3eb099"),
    (47768, "15ba6f1a97b6fc9cf283f8a693980233"),
    (47840, "701edc76806d06211e9d7424e956a8d4"),
    (63708, "2b54b616dfe2b7af7803919609bc6456"),
}
DFLIGHT_DIRECTIONAL_DESCRIPTOR_MASK = 0x00000003
DFLIGHT_CHARACTER_DESCRIPTOR_MASK = 0x04000000
CONTACT_CONSTANT_SLOT = 13
CONTACT_MASK_SLOT = 46
WRAPPED_GRASS_CONSTANT_SLOT = 11
HAIR_SPECULAR_CONSTANT_SLOT = 10
BASIC_WETNESS_CONSTANT_SLOT = 9
SURFACE_CLASS_SLOT = 47
MUL_OPCODE = 0x38
DIV_OPCODE = 0x0E
MOV_OPCODE = 0x36
WRAPPED_NDOTL_INSTRUCTION = (
    0x06002036,
    0x00100012,
    0x00000001,
    0x8010003A,
    0x00000041,
    0x00000001,
)


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
    parser.add_argument("--dispatch-binary", type=Path, required=True)
    parser.add_argument("--dispatch-header", type=Path, required=True)
    parser.add_argument("--resolve-binary", type=Path, required=True)
    parser.add_argument("--resolve-header", type=Path, required=True)
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


def directional_pixel_shaders(root: Path) -> list[census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    unique: dict[tuple[int, str], census.DxbcContainer] = {}
    for item in inventory.containers:
        if (
            item.family != "DFLight"
            or item.stage != "PS"
            or item.key is None
            or item.key & DFLIGHT_CHARACTER_DESCRIPTOR_MASK
            or not item.key & DFLIGHT_DIRECTIONAL_DESCRIPTOR_MASK
        ):
            continue
        unique.setdefault(item.identity, item)
    return sorted(
        unique.values(),
        key=lambda item: (item.size, item.checksum),
    )


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
        "dcl_constantbuffer CB13[3], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t46",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_wrapped_grass_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "WrappedGrassTransform.hlsl"
    )
    output = temporary / "WrappedGrassTransform.dxbc"
    assembly = temporary / "WrappedGrassTransform.asm.txt"
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
        "wrapped-grass transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB11[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.x",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "wrapped-grass transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_hair_specular_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "HairSpecularTransform.hlsl"
    )
    output = temporary / "HairSpecularTransform.dxbc"
    assembly = temporary / "HairSpecularTransform.asm.txt"
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
        "hair-specular transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB10[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.xyz",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "hair-specular transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_basic_wetness_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "BasicWetnessTransform.hlsl"
    )
    output = temporary / "BasicWetnessTransform.dxbc"
    assembly = temporary / "BasicWetnessTransform.asm.txt"
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
        "basic-wetness transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB9[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.xyz",
        "dcl_input_ps linear v2.xyzw",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
        "dcl_output o0.xyzw",
        "dcl_output o1.xyzw",
    ):
        if required not in text:
            raise ContractError(
                "basic-wetness transform assembly changed: " + required
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


def wrapped_grass_template_contract(
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
            and operands[0].immediate_indices[0]
            == WRAPPED_GRASS_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            resource_declaration = words[start:end]
    if constant_declaration is None or resource_declaration is None:
        raise ContractError("wrapped-grass template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("wrapped-grass template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("wrapped-grass template contains an early return")
    return [resource_declaration, constant_declaration], transform[:-1], temp_count


def hair_specular_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    owns_gbuffer_material = False
    owns_surface_class = False
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == HAIR_SPECULAR_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (0,)
        ):
            owns_gbuffer_material = True
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            owns_surface_class = True
    if (
        constant_declaration is None
        or not owns_gbuffer_material
        or not owns_surface_class
    ):
        raise ContractError("hair-specular template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("hair-specular template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("hair-specular template contains an early return")
    # Wrapped Grass already contributes the shared t47 declaration.
    return [constant_declaration], transform[:-1], temp_count


def basic_wetness_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    owns_surface_class = False
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == BASIC_WETNESS_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            owns_surface_class = True
    if constant_declaration is None or not owns_surface_class:
        raise ContractError("basic-wetness template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("basic-wetness template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("basic-wetness template contains an early return")
    # Wrapped Grass already contributes the shared t47 declaration.
    return [constant_declaration], transform[:-1], temp_count


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


def remap_wrapped_grass_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("wrapped-grass template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction,
                    operand,
                    1,
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("wrapped-grass template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("wrapped-grass template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                1,
            )
    return replace_instruction_operands(instruction, replacements)


def remap_hair_specular_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("hair-specular template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("hair-specular template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("hair-specular template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction, operand, 1
            )
    return replace_instruction_operands(instruction, replacements)


def remap_basic_wetness_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("basic-wetness template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            elif operand.immediate_indices == (2,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 0
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("basic-wetness template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 0
                )
            elif operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            else:
                raise ContractError("basic-wetness template output changed")
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


def patch_shader(
    original: bytes,
    contact_template: bytes,
    wrapped_grass_template: bytes,
    hair_specular_template: bytes,
    basic_wetness_template: bytes,
) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temps = words[temp_declaration[0] + 1]
    if original_temps == 0 or original_temps > 4096:
        raise ContractError(
            f"directional DFLight temporary register count is invalid: {original_temps}"
        )
    declarations, transform, template_temps = template_contract(
        contact_template
    )
    wrapped_declarations, wrapped_transform, wrapped_template_temps = (
        wrapped_grass_template_contract(wrapped_grass_template)
    )
    hair_declarations, hair_transform, hair_template_temps = (
        hair_specular_template_contract(hair_specular_template)
    )
    wetness_declarations, wetness_transform, wetness_template_temps = (
        basic_wetness_template_contract(basic_wetness_template)
    )
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
    original_resource_slots = {
        int(operands[0].immediate_indices[0])
        for start, end in instructions(words)
        if (words[start] & 0x7FF) == OPCODE_DCL_RESOURCE
        if (operands := executable_operands(words, start, end))
        if operands[0].operand_type == OPERAND_RESOURCE
        and len(operands[0].immediate_indices) == 1
        and operands[0].immediate_indices[0] is not None
    }
    if 0 not in original_resource_slots:
        raise ContractError(
            "directional DFLight lost the G-buffer material resource at t0"
        )
    first_scratch = original_temps
    visibility_scratch = first_scratch + template_temps
    wrapped_first_scratch = visibility_scratch + 1
    hair_first_scratch = wrapped_first_scratch + wrapped_template_temps
    wetness_first_scratch = hair_first_scratch + hair_template_temps

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

    transformed_wrapped: list[int] = []
    for instruction in wrapped_transform:
        transformed_wrapped.extend(
            remap_wrapped_grass_instruction(
                instruction,
                wrapped_first_scratch,
                wrapped_template_temps,
            )
        )

    transformed_hair: list[int] = []
    for instruction in hair_transform:
        transformed_hair.extend(
            remap_hair_specular_instruction(
                instruction,
                hair_first_scratch,
                hair_template_temps,
            )
        )

    transformed_wetness: list[int] = []
    for instruction in wetness_transform:
        transformed_wetness.extend(
            remap_basic_wetness_instruction(
                instruction,
                wetness_first_scratch,
                wetness_template_temps,
            )
        )

    wrapped_sites = [
        (start, end)
        for start, end in body
        if tuple(words[start:end]) == WRAPPED_NDOTL_INSTRUCTION
    ]
    if len(wrapped_sites) != 1:
        raise ContractError(
            "directional DFLight wrapped NdotL instruction changed"
        )
    wrapped_site = wrapped_sites[0]

    prefix = words[: temp_declaration[0]]
    for declaration in declarations:
        prefix.extend(declaration)
    for declaration in wrapped_declarations:
        prefix.extend(declaration)
    for declaration in hair_declarations:
        prefix.extend(declaration)
    for declaration in wetness_declarations:
        prefix.extend(declaration)
    updated_temps = words[temp_declaration[0] : temp_declaration[1]]
    updated_temps[1] = wetness_first_scratch + wetness_template_temps
    rewritten: list[int] = []
    for start, end in body:
        if (start, end) == o1_write:
            rewritten.extend(transformed_wetness)
            rewritten.extend(transformed_hair)
            rewritten.extend(transformed)
            rewritten.extend(multiply_rgb(1, visibility_scratch))
        elif (start, end) == o0_write:
            rewritten.extend(multiply_rgb(0, visibility_scratch))
        rewritten.extend(words[start:end])
        if (start, end) == wrapped_site:
            rewritten.extend(transformed_wrapped)

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
        "dcl_resource_structured t1, 32",
        "dcl_resource_texturecube (float,float,float,float) t2",
        "dcl_uav_typed_texture2d (unorm,unorm,unorm,unorm) u0",
        "dcl_constantbuffer CB2[46], dynamicIndexed",
        "dcl_constantbuffer CB8[1], immediateIndexed",
        "dcl_constantbuffer CB12[51], dynamicIndexed",
        "dcl_constantbuffer CB13[4], immediateIndexed",
        "dcl_sampler s0, mode_default",
        "dcl_tgsm_structured g0, 4, 384",
        "dcl_tgsm_structured g1, 4, 384",
        "dcl_thread_group 64, 1, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow mask compute assembly changed: " + required
            )
    return output.read_bytes()


def compile_dispatch_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowDispatchCS.hlsl"
    )
    output = temporary / "ContactShadowDispatchCS.dxbc"
    assembly = temporary / "ContactShadowDispatchCS.asm.txt"
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
        "contact-shadow dispatch compute compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB2[3], immediateIndexed",
        "dcl_constantbuffer CB12[12], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_uav_structured u0, 32",
        "dcl_uav_raw u1",
        "dcl_thread_group 1, 1, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow dispatch compute assembly changed: " + required
            )
    return output.read_bytes()


def compile_resolve_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowResolveCS.hlsl"
    )
    output = temporary / "ContactShadowResolveCS.dxbc"
    assembly = temporary / "ContactShadowResolveCS.asm.txt"
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
        "contact-shadow directional resolve compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t1",
        "dcl_resource_texturecube (float,float,float,float) t2",
        "dcl_uav_typed_texture2d (unorm,unorm,unorm,unorm) u0",
        "dcl_constantbuffer CB2[46], dynamicIndexed",
        "dcl_constantbuffer CB8[1], immediateIndexed",
        "dcl_constantbuffer CB12[48], dynamicIndexed",
        "dcl_constantbuffer CB13[4], immediateIndexed",
        "dcl_sampler s0, mode_default",
        "dcl_thread_group 8, 8, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow resolve assembly changed: " + required
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


def write_shader_family_header(
    path: Path,
    candidates: list[tuple[census.DxbcContainer, bytes]],
) -> None:
    rows = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "",
    ]
    symbols: list[str] = []
    for index, (_, data) in enumerate(candidates):
        symbol = f"fo4vr_cs_contact_shadows_dflight_{index:03d}"
        symbols.append(symbol)
        rows.append(f"inline constexpr unsigned char {symbol}[] = {{")
        for offset in range(0, len(data), 16):
            values = ", ".join(
                f"0x{value:02x}" for value in data[offset:offset + 16]
            )
            rows.append(f"    {values},")
        rows.extend(("};", ""))

    rows.extend(
        (
            "struct Fo4vrCsContactShadowShaderContract",
            "{",
            "    std::size_t originalSize;",
            "    std::array<unsigned char, 16> originalChecksum;",
            "    const unsigned char* replacementBytecode;",
            "    std::size_t replacementBytecodeLength;",
            "};",
            "",
            "inline constexpr std::array<",
            "    Fo4vrCsContactShadowShaderContract,",
            f"    {len(candidates)}> fo4vr_cs_contact_shadow_dflight_contracts{{{{",
        )
    )
    for (item, _), symbol in zip(candidates, symbols, strict=True):
        checksum = ", ".join(
            f"0x{value:02x}" for value in bytes.fromhex(item.checksum)
        )
        rows.append(
            "    Fo4vrCsContactShadowShaderContract{ "
            f"{item.size}, {{ {checksum} }}, {symbol}, sizeof({symbol}) }},"
        )
    rows.extend(("}};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def main() -> int:
    args = arguments()
    root = args.root.resolve()
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.compute_binary.parent.mkdir(parents=True, exist_ok=True)
    args.compute_header.parent.mkdir(parents=True, exist_ok=True)
    args.dispatch_binary.parent.mkdir(parents=True, exist_ok=True)
    args.dispatch_header.parent.mkdir(parents=True, exist_ok=True)
    args.resolve_binary.parent.mkdir(parents=True, exist_ok=True)
    args.resolve_header.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fo4vr-contact-shadows-") as folder:
        temporary = Path(folder)
        canonical_original = original_shader(root)
        originals = directional_pixel_shaders(root)
        template = compile_template(root, args.fxc.resolve(), temporary)
        wrapped_grass_template = compile_wrapped_grass_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        hair_specular_template = compile_hair_specular_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        basic_wetness_template = compile_basic_wetness_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        compute = compile_compute_shader(root, args.fxc.resolve(), temporary)
        dispatch = compile_dispatch_shader(
            root,
            args.fxc.resolve(),
            temporary,
        )
        resolve = compile_resolve_shader(
            root,
            args.fxc.resolve(),
            temporary,
        )
        candidates: list[tuple[census.DxbcContainer, bytes]] = []
        for original in originals:
            try:
                candidate = patch_shader(
                    original.data,
                    template,
                    wrapped_grass_template,
                    hair_specular_template,
                    basic_wetness_template,
                )
            except ContractError:
                continue
            candidates.append((original, candidate))

        compatible_identities = {
            item.identity for item, _ in candidates
        }
        if compatible_identities != EXPECTED_COMPATIBLE_IDENTITIES:
            missing = sorted(
                EXPECTED_COMPATIBLE_IDENTITIES - compatible_identities
            )
            unexpected = sorted(
                compatible_identities - EXPECTED_COMPATIBLE_IDENTITIES
            )
            raise ContractError(
                "structurally compatible directional DFLight inventory changed: "
                f"missing={missing}, unexpected={unexpected}"
            )

        required_assembly = (
            "dcl_constantbuffer CB13[3], immediateIndexed",
            "dcl_constantbuffer CB11[1], immediateIndexed",
            "dcl_constantbuffer CB10[1], immediateIndexed",
            "dcl_constantbuffer CB9[1], immediateIndexed",
            "dcl_resource_texture2d (float,float,float,float) t46",
            "dcl_resource_texture2d (float,float,float,float) t47",
            "l(0.212600, 0.715200, 0.072200",
            "l(0.140000)",
            "dcl_output o0.xyzw",
            "dcl_output o1.xyzw",
        )
        canonical_candidate: bytes | None = None
        for index, (original, candidate) in enumerate(candidates):
            candidate_path = temporary / f"ContactShadowsDFLight_{index:03d}.dxbc"
            assembly_path = temporary / f"ContactShadowsDFLight_{index:03d}.asm.txt"
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
                f"contact-shadow candidate {index} validation",
            )
            assembly = assembly_path.read_text(encoding="utf-8")
            for required in required_assembly:
                if required not in assembly:
                    raise ContractError(
                        f"contact-shadow candidate {index} validation failed: "
                        + required
                    )
            if sum(
                line.strip() == "ret" for line in assembly.splitlines()
            ) != 1:
                raise ContractError(
                    f"contact-shadow candidate {index} contains an injected early return"
                )
            if original.data == canonical_original:
                canonical_candidate = candidate

        if canonical_candidate is None:
            raise ContractError(
                "canonical directional DFLight candidate left the compatible family"
            )
        args.binary.write_bytes(canonical_candidate)
        write_shader_family_header(args.header, candidates)
        args.compute_binary.write_bytes(compute)
        write_header(
            args.compute_header,
            compute,
            "fo4vr_cs_contact_shadow_mask",
        )
        args.dispatch_binary.write_bytes(dispatch)
        write_header(
            args.dispatch_header,
            dispatch,
            "fo4vr_cs_contact_shadow_dispatch",
        )
        args.resolve_binary.write_bytes(resolve)
        write_header(
            args.resolve_header,
            resolve,
            "fo4vr_cs_contact_shadow_resolve",
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError) as error:
        print(f"contact-shadow generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
