from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census
from dxbc_transform import (
    OPCODE_CUSTOMDATA,
    OPCODE_DCL_CONSTANT_BUFFER,
    OPCODE_DCL_RESOURCE,
    OPCODE_RET,
    OPERAND_CONSTANT_BUFFER,
    OPERAND_INPUT,
    OPERAND_OUTPUT,
    OPERAND_RESOURCE,
    OPERAND_SAMPLER,
    OPERAND_TEMP,
    DxbcChunk,
    Operand,
    TransformError as ContractError,
    build_dxbc,
    executable_operands,
    instructions,
    pack_words,
    replace_operand_with_temp,
    shader_declarations_and_body,
    shader_words,
)


EXPECTED_CONTRACT_COUNT = 41
EXPECTED_ALIAS_COUNT = 83
SAMPLE_L_OPCODE = 0x48
MUL_OPCODE = 0x38
ADD_OPCODE = 0x00
MAD_OPCODE = 0x32
MOV_OPCODE = 0x36
IBL_CONSTANT_SLOT = 5
BASIC_WETNESS_CONSTANT_SLOT = 9
VANILLA_ENVIRONMENT_SLOT = 8
MATERIAL_DATA_SLOT = 3
DFLIGHT_ALBEDO_SLOT = 29
PUBLISHED_ENVIRONMENT_SLOT = 30
PUBLISHED_VALIDITY_SLOT = 31
PREVIOUS_PUBLISHED_ENVIRONMENT_SLOT = 32
PREVIOUS_PUBLISHED_VALIDITY_SLOT = 33
SURFACE_CLASS_SLOT = 47
SCENE_DEPTH_SLOT = 7
SCENE_CONSTANT_SLOT = 12
ENVIRONMENT_SAMPLER_SLOT = 8
MATERIAL_SAMPLER_SLOT = 3
FIRST_RESOURCE_ID = 1054
SURFACE_ANCHORED_IDENTITIES = {
    (9348, "93edc6af41cbb2d290962e995a4fce25"),
    (9564, "4d6870ba7d5498e729b3195f7dcdf978"),
    (11100, "eb839491ab08ee92fc485990945bbec5"),
    (11316, "fc24da7bbdad0360e221fc9e45ad2e9f"),
}

CONTRACT_PATTERN = re.compile(
    r"\{\s*(\d+)\s*,\s*"
    r'detail::dxbcChecksum\("([0-9a-fA-F]{32})"\)\s*\}'
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate exact-identity FO4VR DFComposite IBL material shaders."
        )
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--surface-anchor-tool", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-assets", action="store_true")
    return parser.parse_args()


def parse_contracts(root: Path) -> list[tuple[int, str]]:
    header = (
        root / "src" / "Features" / "ibl" / "IblCaptureProbeModel.h"
    ).read_text(encoding="utf-8")
    declaration = header.find("kCaptureProbeContracts")
    boundary = header.find("kDFCompositeBoundaryContracts", declaration)
    if declaration < 0 or boundary < 0:
        raise ContractError("IBL capture-probe contract arrays are missing")
    contracts = [
        (int(size), checksum.lower())
        for size, checksum in CONTRACT_PATTERN.findall(
            header[declaration:boundary]
        )
    ]
    if (
        len(contracts) != EXPECTED_CONTRACT_COUNT
        or len(set(contracts)) != EXPECTED_CONTRACT_COUNT
    ):
        raise ContractError(
            "IBL material generation requires the exact 41-identity capture set"
        )
    return contracts


def exact_originals(
    root: Path,
    contracts: list[tuple[int, str]],
) -> list[census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    composite = [
        item
        for item in inventory.containers
        if item.family == "DFComposite" and item.stage == "PS"
    ]
    by_identity: dict[tuple[int, str], list[census.DxbcContainer]] = {}
    for item in composite:
        by_identity.setdefault(item.identity, []).append(item)
    if any(contract not in by_identity for contract in contracts):
        raise ContractError(
            "the active FO4VR FXP no longer contains every IBL material identity"
        )
    alias_count = sum(len(by_identity[contract]) for contract in contracts)
    if alias_count != EXPECTED_ALIAS_COUNT:
        raise ContractError(
            f"IBL material aliases changed: expected 83, found {alias_count}"
        )
    return [by_identity[contract][0] for contract in contracts]


def run_fxc(arguments: list[str], label: str) -> None:
    result = subprocess.run(
        arguments,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        raise ContractError(f"fxc failed for {label}: {details}")


def compile_template(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "IBL"
        / "IblMaterialBlendTemplate.hlsl"
    )
    source_text = source.read_text(encoding="utf-8")
    for required in (
        "VanillaEnvironment : register(t8)",
        "DFLightAlbedo : register(t29)",
        "PublishedEnvironment : register(t30)",
        "PublishedValidity : register(t31)",
        "PreviousPublishedEnvironment : register(t32)",
        "PreviousPublishedValidity : register(t33)",
        "EnvironmentSampler : register(s8)",
        "MaterialSampler : register(s3)",
        "IblMaterialConstants : register(b5)",
        "BasicWetnessSettings : register(b9)",
        "SurfaceClass : register(t47)",
        "if (IblWeight > 1.0 / 255.0)",
        "saturate(validity * IblWeight)",
        "const float3 dynamicDirection = -input.DirectionAndArray.xyz",
        "EnvironmentTransitionWeight",
        "PreviousEnvironmentAvailable",
        "lerp(vanilla.xyz, published, weight)",
        "ComplexMaterialWeight",
        "input.EncodedMaterialTag",
        "[branch]",
        "retainedDiffuse / max(1.0 - metalness, 1.0 / 255.0)",
        "BasicWetnessMaterialParams.x",
    ):
        if required not in source_text:
            raise ContractError(
                f"IBL material template is missing contract: {required}"
            )
    output = temporary / "IblMaterialBlendTemplate.dxbc"
    assembly = temporary / "IblMaterialBlendTemplate.asm.txt"
    run_fxc(
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
        "IBL material transform template",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB5[1], immediateIndexed",
        "dcl_constantbuffer CB9[2], immediateIndexed",
        "dcl_sampler s3, mode_default",
        "dcl_sampler s8, mode_default",
        "dcl_resource_texturecubearray (float,float,float,float) t8",
        "dcl_resource_texture2d (float,float,float,float) t29",
        "dcl_resource_texturecube (float,float,float,float) t30",
        "dcl_resource_texturecube (float,float,float,float) t31",
        "dcl_resource_texturecube (float,float,float,float) t32",
        "dcl_resource_texturecube (float,float,float,float) t33",
        "dcl_resource_texture2d (float,float,float,float) t47",
    ):
        if required not in text:
            raise ContractError(
                "IBL material template assembly changed: " + required
            )
    return output.read_bytes()


def apply_surface_anchor(
    original: census.DxbcContainer,
    tool: Path,
    temporary: Path,
    name: str,
) -> tuple[bytes, bool]:
    if original.identity not in SURFACE_ANCHORED_IDENTITIES:
        return original.data, False
    source = temporary / f"{name}.surface-anchor-input.dxbc"
    output = temporary / f"{name}.surface-anchor-output.dxbc"
    source.write_bytes(original.data)
    result = subprocess.run(
        [str(tool), str(source), str(output)],
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0 or not output.is_file():
        details = (result.stdout + result.stderr).strip()
        raise ContractError(
            f"{name} surface-anchor transform failed "
            f"({result.returncode}): {details}"
        )
    candidate = output.read_bytes()
    if candidate == original.data:
        raise ContractError(f"{name} surface-anchor transform was a no-op")
    return candidate, True


def declaration_for_slot(
    words: list[int],
    opcode: int,
    operand_type: int,
    slot: int,
) -> list[int]:
    matches: list[list[int]] = []
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != opcode:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == operand_type
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0] == slot
        ):
            matches.append(words[start:end])
    if len(matches) != 1:
        raise ContractError(
            f"template must declare exactly one opcode {opcode:#x} slot {slot}"
        )
    return matches[0]


def template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    declarations = [
        declaration_for_slot(
            words,
            OPCODE_DCL_CONSTANT_BUFFER,
            OPERAND_CONSTANT_BUFFER,
            IBL_CONSTANT_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_CONSTANT_BUFFER,
            OPERAND_CONSTANT_BUFFER,
            BASIC_WETNESS_CONSTANT_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            DFLIGHT_ALBEDO_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            PUBLISHED_ENVIRONMENT_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            PUBLISHED_VALIDITY_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            PREVIOUS_PUBLISHED_ENVIRONMENT_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            PREVIOUS_PUBLISHED_VALIDITY_SLOT,
        ),
        declaration_for_slot(
            words,
            OPCODE_DCL_RESOURCE,
            OPERAND_RESOURCE,
            SURFACE_CLASS_SLOT,
        ),
    ]
    temp_declaration, _, body = shader_declarations_and_body(words)
    template_temp_count = words[temp_declaration[0] + 1]
    if template_temp_count == 0 or template_temp_count > 16:
        raise ContractError("IBL material template temporary count changed")
    body_words = [words[start:end] for start, end in body]
    opcodes = [instruction[0] & 0x7FF for instruction in body_words]
    if len(opcodes) < 10 or opcodes[-1] != OPCODE_RET:
        raise ContractError(
            "IBL material template instruction sequence changed: "
            + ", ".join(f"{opcode:#x}" for opcode in opcodes)
        )
    output_writes = [
        operand
        for instruction in body_words[:-1]
        for operand in executable_operands(instruction, 0, len(instruction))
        if operand.operand_type == OPERAND_OUTPUT
    ]
    if not output_writes or any(
        operand.immediate_indices != (0,) for operand in output_writes
    ):
        raise ContractError("IBL material template output contract changed")
    return declarations, body_words[:-1], template_temp_count


def replace_instruction_operands(
    instruction: list[int],
    replacements: dict[int, list[int]],
) -> list[int]:
    result: list[int] = []
    cursor = 0
    operands = {
        operand.start: operand
        for operand in executable_operands(instruction, 0, len(instruction))
    }
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is not None:
            operand = operands.get(cursor)
            if operand is None:
                raise ContractError("operand replacement does not start at an operand")
            result.extend(replacement)
            cursor = operand.end
        else:
            result.append(instruction[cursor])
            cursor += 1
    if len(result) > 0x7F:
        raise ContractError("rewritten IBL instruction exceeds the token limit")
    result[0] = (result[0] & ~(0x7F << 24)) | (len(result) << 24)
    return result


def replace_scalar_operand_with_temp_component(
    instruction: list[int],
    operand: Operand,
    register: int,
    component: int,
) -> list[int]:
    if component < 0 or component > 3:
        raise ContractError("temporary component is invalid")
    result = replace_operand_with_temp(instruction, operand, register)
    if ((result[0] >> 2) & 0x3) != 2:
        raise ContractError("template material tag is no longer scalar-selected")
    result[0] = (result[0] & ~(0x3 << 4)) | (component << 4)
    return result


def remap_template_instruction(
    instruction: list[int],
    coordinate: list[int],
    lod: list[int],
    material_tag_register: int,
    material_coordinate: list[int],
    first_scratch: int,
    template_temp_count: int,
    output_scratch: int,
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] >= template_temp_count
            ):
                raise ContractError(
                    "IBL material template uses an unexpected temporary"
                )
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = coordinate
            elif operand.immediate_indices == (1,):
                token = instruction[operand.start]
                selection_mode = (token >> 2) & 0x3
                if selection_mode == 2:
                    component = (token >> 4) & 0x3
                    if component == 0:
                        replacements[operand.start] = lod
                    elif component == 1:
                        replacements[operand.start] = (
                            replace_scalar_operand_with_temp_component(
                                instruction,
                                operand,
                                material_tag_register,
                                0,
                            )
                        )
                    elif component in (2, 3):
                        if len(material_coordinate) != 2:
                            raise ContractError(
                                "DFComposite material coordinate changed shape"
                            )
                        coordinate_token = material_coordinate[0]
                        if ((coordinate_token >> 2) & 0x3) != 1:
                            raise ContractError(
                                "DFComposite material coordinate is not swizzled"
                            )
                        coordinate_component = (
                            coordinate_token >>
                            (4 + (component - 2) * 2)
                        ) & 0x3
                        replacements[operand.start] = temp_scalar_operand(
                            material_coordinate[1],
                            coordinate_component,
                        )
                    else:
                        raise ContractError(
                            "IBL material template scalar input packing changed"
                        )
                elif selection_mode == 1:
                    swizzle = (token >> 4) & 0xFF
                    used_components = {
                        (swizzle >> (component * 2)) & 0x3
                        for component in range(4)
                    }
                    if not used_components.issubset({2, 3}):
                        raise ContractError(
                            "IBL material template UV input packing changed"
                        )
                    replacements[operand.start] = material_coordinate
                else:
                    raise ContractError(
                        "IBL material template input selection changed"
                    )
            else:
                raise ContractError(
                    "IBL material template uses an unexpected input"
                )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError(
                    "IBL material template uses an unexpected output"
                )
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                output_scratch,
            )
    return replace_instruction_operands(instruction, replacements)


def temp_scalar_operand(register: int, component: int) -> list[int]:
    if register < 0 or component < 0 or component > 3:
        raise ContractError("temporary scalar operand is invalid")
    return [0x0010000A | (component << 4), register]


def temp_mask_operand(register: int, mask: int) -> list[int]:
    if register < 0 or mask <= 0 or mask > 0xF:
        raise ContractError("temporary mask operand is invalid")
    return [0x00100002 | (mask << 4), register]


def move_temp_component(
    destination_register: int,
    destination_component: int,
    source_register: int,
    source_component: int,
) -> list[int]:
    instruction = [
        MOV_OPCODE,
        *temp_mask_operand(
            destination_register, 1 << destination_component
        ),
        *temp_scalar_operand(source_register, source_component),
    ]
    instruction[0] |= len(instruction) << 24
    return instruction


def final_material_move(destination: list[int], source_register: int) -> list[int]:
    destination_mask = (destination[0] >> 4) & 0xF
    source_swizzles = {
        0x7: 0x246,  # xyz <- xyz
        0xB: 0xA46,  # xyw <- xyz
        0xD: 0x946,  # xzw <- xyz
    }
    swizzle = source_swizzles.get(destination_mask)
    if swizzle is None:
        raise ContractError(
            f"unsupported DFComposite environment destination mask {destination_mask:#x}"
        )
    source = [0x00100000 | swizzle, source_register]
    result = [MOV_OPCODE, *destination, *source]
    result[0] |= len(result) << 24
    return result


def patch_shader(
    original: bytes,
    declarations: list[list[int]],
    transform: list[list[int]],
    template_temp_count: int,
) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temp_count = words[temp_declaration[0] + 1]
    if original_temp_count == 0 or original_temp_count > 4093:
        raise ContractError("DFComposite temporary-register count is invalid")

    occupied_buffers: set[int] = set()
    occupied_resources: set[int] = set()
    scene_buffer_declaration: tuple[int, int] | None = None
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        if opcode == OPCODE_CUSTOMDATA:
            continue
        if opcode not in (OPCODE_DCL_CONSTANT_BUFFER, OPCODE_DCL_RESOURCE):
            continue
        operands = executable_operands(words, start, end)
        if not operands or not operands[0].immediate_indices:
            continue
        if opcode == OPCODE_DCL_CONSTANT_BUFFER:
            slot = int(operands[0].immediate_indices[0])
            occupied_buffers.add(slot)
            if slot == SCENE_CONSTANT_SLOT:
                if scene_buffer_declaration is not None:
                    raise ContractError("DFComposite declares b12 repeatedly")
                scene_buffer_declaration = (start, end)
        else:
            occupied_resources.add(int(operands[0].immediate_indices[0]))
    if IBL_CONSTANT_SLOT in occupied_buffers:
        raise ContractError("DFComposite unexpectedly owns b5")
    if scene_buffer_declaration is None:
        raise ContractError("DFComposite does not expose the verified b12 scene buffer")
    scene_start, scene_end = scene_buffer_declaration
    if scene_end - scene_start != 4:
        raise ContractError("DFComposite b12 declaration changed shape")
    scene_rows = words[scene_end - 1]
    if scene_rows not in (51, 61, 77, 82):
        raise ContractError(
            f"DFComposite b12 row contract changed: {scene_rows}"
        )
    if {
        DFLIGHT_ALBEDO_SLOT,
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
        PREVIOUS_PUBLISHED_ENVIRONMENT_SLOT,
        PREVIOUS_PUBLISHED_VALIDITY_SLOT,
    } & occupied_resources:
        raise ContractError(
            "DFComposite unexpectedly owns an injected t29..t33 slot"
        )
    if SCENE_DEPTH_SLOT not in occupied_resources:
        raise ContractError("DFComposite does not expose the verified t7 depth")

    environment_samples: list[tuple[int, int, list[Operand]]] = []
    for start, end in body:
        if (words[start] & 0x7FF) != SAMPLE_L_OPCODE:
            continue
        operands = executable_operands(words, start, end)
        if any(
            operand.operand_type == OPERAND_RESOURCE
            and operand.immediate_indices == (VANILLA_ENVIRONMENT_SLOT,)
            for operand in operands
        ):
            environment_samples.append((start, end, operands))
    if len(environment_samples) != 1:
        raise ContractError(
            "DFComposite must contain exactly one t8 sample_l instruction"
        )
    sample_start, sample_end, sample_operands = environment_samples[0]
    if len(sample_operands) != 5:
        raise ContractError("DFComposite t8 sample_l changed operand shape")
    destination, coordinate, resource, sampler, lod = sample_operands
    if (
        destination.operand_type != OPERAND_TEMP
        or len(destination.immediate_indices) != 1
        or coordinate.operand_type != OPERAND_TEMP
        or len(coordinate.immediate_indices) != 1
        or resource.immediate_indices != (VANILLA_ENVIRONMENT_SLOT,)
        or sampler.operand_type != OPERAND_SAMPLER
        or sampler.immediate_indices != (ENVIRONMENT_SAMPLER_SLOT,)
        or lod.operand_type != OPERAND_TEMP
        or len(lod.immediate_indices) != 1
    ):
        raise ContractError("DFComposite t8 sample_l no longer has the verified shape")

    destination_words = words[destination.start : destination.end]
    coordinate_words = words[coordinate.start : coordinate.end]
    lod_words = words[lod.start : lod.end]
    destination_mask = (destination_words[0] >> 4) & 0xF
    expected_resource_swizzle = {
        0x7: 0xE46,
        0xB: 0xB46,
        0xD: 0x9C6,
    }.get(destination_mask)
    if expected_resource_swizzle is None or (
        resource.end - resource.start != 2
        or (words[resource.start] & 0xFFF) != expected_resource_swizzle
    ):
        raise ContractError(
            "DFComposite t8 sample no longer maps RGB into a verified destination shape"
        )

    material_samples: list[tuple[int, int, list[Operand]]] = []
    for start, end in body:
        if start >= sample_start or (words[start] & 0x7FF) != SAMPLE_L_OPCODE:
            continue
        operands = executable_operands(words, start, end)
        if len(operands) != 5:
            continue
        candidate_destination, candidate_coordinate, candidate_resource, candidate_sampler, _ = operands
        if (
            candidate_resource.operand_type == OPERAND_RESOURCE
            and candidate_resource.immediate_indices == (MATERIAL_DATA_SLOT,)
            and candidate_sampler.operand_type == OPERAND_SAMPLER
            and candidate_sampler.immediate_indices == (MATERIAL_SAMPLER_SLOT,)
            and candidate_destination.operand_type == OPERAND_TEMP
            and candidate_coordinate.operand_type == OPERAND_TEMP
            and len(candidate_coordinate.immediate_indices) == 1
        ):
            material_samples.append((start, end, operands))
    if len(material_samples) != 1:
        raise ContractError(
            "DFComposite must contain one verified pre-environment t3 sample_l"
        )
    material_start, material_end, material_operands = material_samples[0]
    material_destination = material_operands[0]
    material_coordinate = material_operands[1]
    material_resource = material_operands[2]
    if (
        len(material_destination.immediate_indices) != 1
        or material_destination.immediate_indices[0] < 0
    ):
        raise ContractError("DFComposite material destination is not a temporary")
    material_destination_words = words[
        material_destination.start : material_destination.end
    ]
    material_destination_mask = (material_destination_words[0] >> 4) & 0xF
    if material_destination_mask == 0:
        raise ContractError("DFComposite material destination has no write mask")
    material_resource_words = words[
        material_resource.start : material_resource.end
    ]
    material_resource_swizzle = (material_resource_words[0] >> 4) & 0xFF
    material_tag_components = [
        component
        for component in range(4)
        if (material_destination_mask & (1 << component)) != 0
        and ((material_resource_swizzle >> (component * 2)) & 0x3) == 3
    ]
    if len(material_tag_components) > 1:
        raise ContractError("DFComposite material sample maps target3.w repeatedly")
    material_tag_component = (
        material_tag_components[0] if material_tag_components else None
    )
    material_spare_component = None
    if material_tag_component is None:
        material_spare_component = next(
            (
                component
                for component in range(4)
                if (material_destination_mask & (1 << component)) == 0
            ),
            None,
        )
        if material_spare_component is None:
            raise ContractError(
                "DFComposite material sample cannot expose target3.w without "
                "changing a live component"
            )
    material_coordinate_words = words[
        material_coordinate.start : material_coordinate.end
    ]

    first_scratch = original_temp_count
    output_scratch = original_temp_count + template_temp_count
    material_tag_scratch = output_scratch + 1
    material_restore_scratch = material_tag_scratch + 1
    replacement: list[int] = []
    for instruction in transform:
        replacement.extend(
            remap_template_instruction(
                instruction,
                coordinate_words,
                lod_words,
                material_tag_scratch,
                material_coordinate_words,
                first_scratch,
                template_temp_count,
                output_scratch,
            )
        )
    replacement.extend(
        final_material_move(destination_words, output_scratch)
    )

    prefix = words[: temp_declaration[0]]
    for declaration in declarations:
        prefix.extend(declaration)
    updated_temp_declaration = words[temp_declaration[0] : temp_declaration[1]]
    updated_temp_declaration[1] = original_temp_count + template_temp_count + 3

    rewritten_body: list[int] = []
    for start, end in body:
        if start == material_start and end == material_end:
            material_sample = list(words[start:end])
            sampled_component = material_tag_component
            if material_spare_component is not None:
                rewritten_body.extend(
                    move_temp_component(
                        material_restore_scratch,
                        0,
                        int(material_destination.immediate_indices[0]),
                        material_spare_component,
                    )
                )
                relative_destination = material_destination.start - start
                material_sample[relative_destination] = (
                    material_sample[relative_destination] & ~(0xF << 4)
                ) | (
                    (material_destination_mask |
                     (1 << material_spare_component)) << 4
                )
                relative_resource = material_resource.start - start
                swizzle_shift = 4 + (material_spare_component * 2)
                material_sample[relative_resource] = (
                    material_sample[relative_resource] & ~(0x3 << swizzle_shift)
                ) | (0x3 << swizzle_shift)
                sampled_component = material_spare_component
            rewritten_body.extend(material_sample)
            rewritten_body.extend(
                move_temp_component(
                    material_tag_scratch,
                    0,
                    int(material_destination.immediate_indices[0]),
                    int(sampled_component),
                )
            )
            if material_spare_component is not None:
                rewritten_body.extend(
                    move_temp_component(
                        int(material_destination.immediate_indices[0]),
                        material_spare_component,
                        material_restore_scratch,
                        0,
                    )
                )
        elif start == sample_start and end == sample_end:
            rewritten_body.extend(replacement)
        else:
            rewritten_body.extend(words[start:end])
    patched_words = [*prefix, *updated_temp_declaration, *rewritten_body]
    patched_words[1] = len(patched_words)
    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def signature_contract(assembly: str) -> str:
    try:
        start = assembly.index("// Input signature:")
        end = assembly.index("ps_5_0", start)
    except ValueError as error:
        raise ContractError("shader assembly is missing signature tables") from error
    return "\n".join(
        line.rstrip()
        for line in assembly[start:end].splitlines()
        if line.startswith("//")
    )


def validate_candidate(
    fxc: Path,
    name: str,
    original: bytes,
    candidate: bytes,
    temporary: Path,
) -> None:
    original_path = temporary / f"{name}.vanilla.dxbc"
    candidate_path = temporary / f"{name}.dxbc"
    original_assembly = temporary / f"{name}.vanilla.asm.txt"
    candidate_assembly = temporary / f"{name}.asm.txt"
    original_path.write_bytes(original)
    candidate_path.write_bytes(candidate)
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/dumpbin",
            "/Fc",
            str(original_assembly),
            str(original_path),
        ],
        f"{name} vanilla",
    )
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/dumpbin",
            "/Fc",
            str(candidate_assembly),
            str(candidate_path),
        ],
        f"{name} replacement",
    )
    original_text = original_assembly.read_text(encoding="utf-8")
    candidate_text = candidate_assembly.read_text(encoding="utf-8")
    if signature_contract(original_text) != signature_contract(candidate_text):
        raise ContractError(f"{name} changed the exact shader signatures")
    original_declarations = census.parse_declarations(original_text)
    candidate_declarations = census.parse_declarations(candidate_text)
    original_buffers = dict(original_declarations.constant_buffers)
    candidate_buffers = dict(candidate_declarations.constant_buffers)
    if candidate_buffers.pop(IBL_CONSTANT_SLOT, None) != 1:
        raise ContractError(f"{name} does not add exact b5[1]")
    if candidate_buffers.pop(BASIC_WETNESS_CONSTANT_SLOT, None) != 2:
        raise ContractError(f"{name} does not add exact b9[2]")
    if candidate_buffers != original_buffers:
        raise ContractError(
            f"{name} changed vanilla constant buffers"
        )
    original_textures = set(original_declarations.textures)
    candidate_textures = set(candidate_declarations.textures)
    if candidate_textures - {
        DFLIGHT_ALBEDO_SLOT,
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
        PREVIOUS_PUBLISHED_ENVIRONMENT_SLOT,
        PREVIOUS_PUBLISHED_VALIDITY_SLOT,
        SURFACE_CLASS_SLOT,
    } != original_textures or not {
        DFLIGHT_ALBEDO_SLOT,
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
        PREVIOUS_PUBLISHED_ENVIRONMENT_SLOT,
        PREVIOUS_PUBLISHED_VALIDITY_SLOT,
        SURFACE_CLASS_SLOT,
    }.issubset(candidate_textures):
        raise ContractError(f"{name} changed the texture contract")
    if (
        candidate_declarations.samplers != original_declarations.samplers
        or candidate_declarations.inputs != original_declarations.inputs
        or candidate_declarations.outputs != original_declarations.outputs
        or candidate_declarations.global_flags
        != original_declarations.global_flags
    ):
        raise ContractError(f"{name} changed vanilla declarations")
    for declaration in (
        "dcl_resource_texture2d (float,float,float,float) t29",
        "dcl_resource_texturecube (float,float,float,float) t30",
        "dcl_resource_texturecube (float,float,float,float) t31",
        "dcl_resource_texturecube (float,float,float,float) t32",
        "dcl_resource_texturecube (float,float,float,float) t33",
        "dcl_resource_texture2d (float,float,float,float) t47",
    ):
        if declaration not in candidate_text:
            raise ContractError(f"{name} is missing {declaration}")
    if len(re.findall(r"\bt30(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t30 exactly once")
    if len(re.findall(r"\bt31(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t31 exactly once")
    if len(re.findall(r"\bt32(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t32 exactly once")
    if len(re.findall(r"\bt33(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t33 exactly once")
    if len(re.findall(r"\bt29(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t29 exactly once")
    if len(re.findall(r"\bt47(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t47 exactly once")
    if len(re.findall(r"\bt3(?:\b|\.)", candidate_text)) != len(
        re.findall(r"\bt3(?:\b|\.)", original_text)
    ):
        raise ContractError(f"{name} added a redundant material-data sample")
    if len(re.findall(r"\bt7(?:\b|\.)", candidate_text)) != len(
        re.findall(r"\bt7(?:\b|\.)", original_text)
    ):
        raise ContractError(f"{name} changed receiver-depth usage")
    if candidate_text.count("if_nz") <= original_text.count("if_nz"):
        raise ContractError(f"{name} did not branch around sparse metal work")
    metal_block_start = re.search(
        r"^\s*sample_l_indexable\(texturecube\).*\bt30(?:\b|\.)",
        candidate_text,
        re.MULTILINE,
    )
    metal_block_end = re.search(
        r"^\s*sample_l_indexable\(texture2d\).*\bt29(?:\b|\.)",
        candidate_text,
        re.MULTILINE,
    )
    if (
        metal_block_start is None
        or metal_block_end is None
        or metal_block_start.start() >= metal_block_end.start()
    ):
        raise ContractError(f"{name} lost its ordered complex-material block")
    metal_block = candidate_text[
        metal_block_start.start() : metal_block_end.end()
    ]
    if not re.search(
        r"add\s+r\d+\.[xyzw]+,\s+-r\d+\.[xyzw]+,\s+l\(1\.000000\)",
        metal_block,
    ):
        raise ContractError(f"{name} lost the encoded-tag subtraction")
    if not re.search(
        r"sample_l_indexable\(texture2d\).*r\d+\.[xyzw]{2,4},\s+t29",
        metal_block,
    ):
        raise ContractError(f"{name} lost the retained-albedo screen coordinate")
    if re.search(r"^\s*dcl_uav", candidate_text, re.MULTILINE):
        raise ContractError(f"{name} unexpectedly declares a UAV")


def format_identity(data: bytes, indent: str) -> list[str]:
    rows = [f"{indent}{{", f"{indent}    {len(data)},", f"{indent}    {{"]
    for offset in range(4, 20, 4):
        values = ", ".join(
            f"0x{value:02X}u" for value in data[offset : offset + 4]
        )
        rows.append(f"{indent}        {values},")
    rows.extend((f"{indent}    }},", f"{indent}}},"))
    return rows


def contract_name(index: int, checksum: str) -> str:
    return f"DFComposite_{index:02d}_{checksum[:8]}"


def resource_name(index: int) -> str:
    return f"IDR_IBL_MATERIAL_{index:02d}_PS"


def render_contracts(
    originals: list[census.DxbcContainer],
    candidates: list[bytes],
) -> str:
    rows = [
        "// Generated by tools/generate_ibl_material_contracts.py.",
        "// Do not edit this file by hand.",
        "constexpr std::array<IblMaterialShaderDefinition, 41>",
        "    kIblMaterialShaderDefinitions{ {",
    ]
    for index, (original, candidate) in enumerate(zip(originals, candidates)):
        rows.extend(
            (
                "        {",
                f'            "{contract_name(index, original.checksum)}",',
                f"            {resource_name(index)},",
            )
        )
        rows.extend(format_identity(original.data, "            "))
        rows.extend(format_identity(candidate, "            "))
        rows.append("        },")
    rows.extend(("    } };", ""))
    return "\n".join(rows)


def verify_resource_contract(root: Path, originals: list[census.DxbcContainer]) -> None:
    header = (root / "src" / "resources.h").read_text(encoding="utf-8")
    script = (root / "src" / "resources.rc").read_text(encoding="utf-8")
    for index, original in enumerate(originals):
        macro = resource_name(index)
        resource_id = FIRST_RESOURCE_ID + index
        expected_header = f"#define {macro} {resource_id}"
        expected_script = (
            f'{macro} RCDATA "../package/Shaders/Community/IBLMaterial/'
            f'{contract_name(index, original.checksum)}.dxbc"'
        )
        if expected_header not in header:
            raise ContractError(f"resource header is missing: {expected_header}")
        if expected_script not in script:
            raise ContractError(f"resource script is missing: {expected_script}")


def main() -> int:
    arguments = parse_arguments()
    if arguments.check == arguments.write_assets:
        print(
            "select exactly one of --check or --write-assets",
            file=sys.stderr,
        )
        return 1
    try:
        root = arguments.root.resolve()
        surface_anchor_tool = arguments.surface_anchor_tool.resolve()
        if not surface_anchor_tool.is_file():
            raise ContractError(
                f"surface-anchor tool is unavailable: {surface_anchor_tool}"
            )
        contracts = parse_contracts(root)
        originals = exact_originals(root, contracts)
        fxc = census.find_fxc(None)
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_cs_ibl_material_"
        ) as directory:
            temporary = Path(directory)
            template = compile_template(root, fxc, temporary)
            (
                declarations,
                transform,
                template_temp_count,
            ) = template_contract(template)
            candidates: list[bytes] = []
            anchored_count = 0
            for index, original in enumerate(originals):
                name = contract_name(index, original.checksum)
                anchored_original, surface_anchored = apply_surface_anchor(
                    original,
                    surface_anchor_tool,
                    temporary,
                    name,
                )
                anchored_count += int(surface_anchored)
                candidate = patch_shader(
                    anchored_original,
                    declarations,
                    transform,
                    template_temp_count,
                )
                validate_candidate(
                    fxc,
                    name,
                    anchored_original,
                    candidate,
                    temporary,
                )
                candidates.append(candidate)
            if anchored_count != len(SURFACE_ANCHORED_IDENTITIES):
                raise ContractError(
                    "IBL generation did not surface-anchor all four exact "
                    "cubemap identities"
                )

        generated = render_contracts(originals, candidates)
        output = arguments.output.resolve()
        candidate_directory = (
            root / "package" / "Shaders" / "Community" / "IBLMaterial"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedIBLMaterial"
        )
        if arguments.write_assets:
            candidate_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            for index, (original, candidate) in enumerate(
                zip(originals, candidates)
            ):
                name = contract_name(index, original.checksum)
                (candidate_directory / f"{name}.dxbc").write_bytes(candidate)
                (verified_directory / f"{name}.dxbc").write_bytes(
                    original.data
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if (
                not output.is_file()
                or output.read_text(encoding="utf-8") != generated
            ):
                raise ContractError(f"generated IBL material contracts are stale: {output}")
            for index, (original, candidate) in enumerate(
                zip(originals, candidates)
            ):
                name = contract_name(index, original.checksum)
                candidate_asset = candidate_directory / f"{name}.dxbc"
                verified_asset = verified_directory / f"{name}.dxbc"
                if (
                    not candidate_asset.is_file()
                    or candidate_asset.read_bytes() != candidate
                ):
                    raise ContractError(
                        f"packaged IBL material replacement is stale: {candidate_asset}"
                    )
                if (
                    not verified_asset.is_file()
                    or verified_asset.read_bytes() != original.data
                ):
                    raise ContractError(
                        f"verified IBL material original is stale: {verified_asset}"
                    )
            verify_resource_contract(root, originals)
        print(
            "IBL material contracts verified: 41 exact DFComposite identities, "
            "including four surface-anchored cubemap permutations; "
            "vanilla t8/s8 fallback and weight-gated, validity-aware "
            "world-direction t29..t33/b5 consumption."
        )
    except (OSError, ContractError, census.CensusError) as error:
        print(f"IBL material contract generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
