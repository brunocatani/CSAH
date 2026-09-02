from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import census_linear_lighting_fxp as census
from dxbc_transform import (
    OPCODE_DCL_RESOURCE,
    OPCODE_RET,
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
    parse_dxbc,
    replace_operand_with_temp,
    shader_declarations_and_body,
    shader_words,
)


FIRST_RESOURCE_ID = 1095
FIRST_AUTHORED_PBR_RESOURCE_ID = 2000
EXPECTED_MATERIAL_CONTRACTS = 288
EXPECTED_DESCRIPTOR_CONTRACTS = 459
EXPECTED_SPECIALIZED_VARIANTS = 106
EXPECTED_AUTHORED_PBR_CONTRACTS = 279
EXPECTED_AUTHORED_PBR_SPECIALIZED_VARIANTS = 105
EXPECTED_GRASS_VERTEX_SHADER_ROWS = 11
EXPECTED_GRASS_VERTEX_SHADER_IDENTITIES = 5
SURFACE_TARGET = 6
PBR_MATERIAL_TARGET = 7
PBR_RMAOS_SLOT = 48
PBR_TEMPLATE_SAMPLER_SLOT = 15
SAMPLE_OPCODES = {0x45, 0x46, 0x47, 0x48, 0x49, 0x4A}
MOV_OPCODE = 0x36

SURFACE_CLASS_ORDINARY = 0
SURFACE_CLASS_GRASS = 1
SURFACE_CLASS_HAIR = 2
SURFACE_CLASS_SKIN = 3
SURFACE_CLASS_TERRAIN = 4
SURFACE_CLASS_NAMES = {
    SURFACE_CLASS_GRASS: "Grass",
    SURFACE_CLASS_HAIR: "Hair",
    SURFACE_CLASS_SKIN: "Skin",
    SURFACE_CLASS_TERRAIN: "Terrain",
}
SURFACE_CLASS_AMBIGUOUS = 0xFFFFFFFF
EXPECTED_UNAMBIGUOUS_MATERIAL_CLASSES = {
    SURFACE_CLASS_ORDINARY: 182,
    SURFACE_CLASS_HAIR: 38,
    SURFACE_CLASS_SKIN: 47,
    SURFACE_CLASS_TERRAIN: 15,
}
EXPECTED_AMBIGUOUS_MATERIAL_CONTRACTS = 6


@dataclass(frozen=True)
class SignatureElement:
    name: str
    semantic_index: int
    system_value: int
    component_type: int
    register: int
    mask: int
    read_write_mask: int
    stream: int
    minimum_precision: int


@dataclass(frozen=True)
class TransformTemplate:
    output_declarations: tuple[tuple[int, ...], ...]
    output_writes: tuple[tuple[int, ...], ...]
    output_signatures: tuple[SignatureElement, ...]


@dataclass(frozen=True)
class AuthoredMaterialTemplate:
    resource_declaration: tuple[int, ...]
    output_declaration: tuple[int, ...]
    output_signature: SignatureElement
    body: tuple[tuple[int, ...], ...]
    temporary_count: int


@dataclass(frozen=True)
class Contract:
    index: int
    name: str
    source: Path
    original_identity: tuple[int, str]


@dataclass(frozen=True)
class SpecializedVariant:
    contract: Contract
    class_code: int


@dataclass(frozen=True)
class DescriptorContract:
    descriptor: int
    contract_index: int
    class_code: int
    variant_index_plus_one: int


@dataclass(frozen=True)
class MaterialClassContract:
    class_code: int
    variant_index_plus_one: int
    grass_variant_index_plus_one: int


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate exact Linear Lighting MRT surface-classification variants."
        )
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--write-assets", action="store_true")
    parser.add_argument("--check", action="store_true")
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
        details = (result.stdout + result.stderr).strip()
        raise ContractError(f"{label} failed: {details}")


def read_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise ContractError("DXBC signature string offset is invalid")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ContractError("DXBC signature string is unterminated")
    return data[offset:end].decode("ascii")


def parse_signature(payload: bytes) -> tuple[int, list[SignatureElement]]:
    if len(payload) < 8:
        raise ContractError("DXBC signature chunk is truncated")
    count, header = struct.unpack_from("<2I", payload, 0)
    if count > 64 or 8 + count * 24 > len(payload):
        raise ContractError("DXBC signature element table is invalid")
    elements: list[SignatureElement] = []
    for index in range(count):
        values = struct.unpack_from("<5I4B", payload, 8 + index * 24)
        elements.append(
            SignatureElement(
                name=read_c_string(payload, values[0]),
                semantic_index=values[1],
                system_value=values[2],
                component_type=values[3],
                register=values[4],
                mask=values[5],
                read_write_mask=values[6],
                stream=values[7],
                minimum_precision=values[8],
            )
        )
    return header, elements


def build_signature(header: int, elements: list[SignatureElement]) -> bytes:
    table_size = 8 + len(elements) * 24
    strings = bytearray()
    offsets: dict[str, int] = {}
    for element in elements:
        if element.name not in offsets:
            offsets[element.name] = table_size + len(strings)
            strings.extend(element.name.encode("ascii") + b"\0")
    output = bytearray(table_size)
    struct.pack_into("<2I", output, 0, len(elements), header)
    for index, element in enumerate(elements):
        struct.pack_into(
            "<5I4B",
            output,
            8 + index * 24,
            offsets[element.name],
            element.semantic_index,
            element.system_value,
            element.component_type,
            element.register,
            element.mask,
            element.read_write_mask,
            element.stream,
            element.minimum_precision,
        )
    output.extend(strings)
    while len(output) % 4:
        output.append(0)
    return bytes(output)


def compile_template(
    root: Path,
    fxc: Path,
    temporary: Path,
    class_code: int,
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "SurfaceClassification"
        / "SurfaceClassOutput.hlsl"
    )
    output = temporary / f"SurfaceClassOutput{class_code}.dxbc"
    assembly = temporary / f"SurfaceClassOutput{class_code}.asm.txt"
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
            f"/DSURFACE_CLASS_CODE={class_code}",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        f"surface-class template {class_code}",
    )
    text = assembly.read_text(encoding="utf-8")
    if (
        "dcl_output o6.x" not in text
        or "dcl_output o7.xyzw" not in text
        or "SV_Target                6" not in text
        or "SV_Target                7" not in text
    ):
        raise ContractError(
            f"surface-class template {class_code} lost MRT6/MRT7"
        )
    return output.read_bytes()


def compile_authored_template(
    root: Path,
    fxc: Path,
    temporary: Path,
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "PBR"
        / "PbrMaterialOutput.hlsl"
    )
    output = temporary / "PbrMaterialOutput.dxbc"
    assembly = temporary / "PbrMaterialOutput.asm.txt"
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
        "authored PBR material template",
    )
    text = assembly.read_text(encoding="utf-8")
    if (
        "dcl_resource_texture2d (float,float,float,float) t48" not in text
        or "dcl_sampler s15" not in text
        or "dcl_output o7.xyzw" not in text
        or "SV_Target                7" not in text
    ):
        raise ContractError("authored PBR material template contract changed")
    return output.read_bytes()


def template_contract(data: bytes) -> TransformTemplate:
    _, chunks, _, words = shader_words(data)
    signature_chunks = [chunk for chunk in chunks if chunk.tag == b"OSGN"]
    if len(signature_chunks) != 1:
        raise ContractError("surface-class template must contain one OSGN chunk")
    _, signature = parse_signature(signature_chunks[0].payload)
    outputs = {
        element.semantic_index: element
        for element in signature
        if element.name == "SV_Target"
        and element.semantic_index in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
        and element.register == element.semantic_index
    }
    if (
        set(outputs) != {SURFACE_TARGET, PBR_MATERIAL_TARGET}
        or outputs[SURFACE_TARGET].mask != 0x1
        or outputs[PBR_MATERIAL_TARGET].mask != 0xF
    ):
        raise ContractError("surface-class template output signature changed")

    declarations: dict[int, tuple[int, ...]] = {}
    output_writes: dict[int, tuple[int, ...]] = {}
    for start, end in instructions(words):
        operands = executable_operands(words, start, end)
        owned_targets = {
            int(operand.immediate_indices[0])
            for operand in operands
            if operand.operand_type == OPERAND_OUTPUT
            and operand.immediate_indices
            and operand.immediate_indices[0]
            in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
        }
        if not owned_targets:
            continue
        if len(owned_targets) != 1:
            raise ContractError("surface-class instruction owns two outputs")
        target = next(iter(owned_targets))
        opcode = words[start] & 0x7FF
        destination = output_writes if opcode == MOV_OPCODE else declarations
        if target in destination:
            raise ContractError(
                f"surface-class template owns target{target} twice"
            )
        destination[target] = tuple(words[start:end])
    if set(declarations) != set(outputs) or set(output_writes) != set(outputs):
        raise ContractError("surface-class template token contract is incomplete")
    return TransformTemplate(
        tuple(declarations[target] for target in sorted(declarations)),
        tuple(output_writes[target] for target in sorted(output_writes)),
        tuple(outputs[target] for target in sorted(outputs)),
    )


def declaration_for_slot(
    words: list[int],
    opcode: int,
    operand_type: int,
    slot: int,
) -> tuple[int, ...]:
    matches: list[tuple[int, ...]] = []
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != opcode:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == operand_type
            and operands[0].immediate_indices == (slot,)
        ):
            matches.append(tuple(words[start:end]))
    if len(matches) != 1:
        raise ContractError(
            f"template must declare opcode {opcode:#x} slot {slot} once"
        )
    return matches[0]


def authored_template_contract(data: bytes) -> AuthoredMaterialTemplate:
    _, chunks, _, words = shader_words(data)
    signature_chunks = [chunk for chunk in chunks if chunk.tag == b"OSGN"]
    if len(signature_chunks) != 1:
        raise ContractError("authored PBR template must contain one OSGN chunk")
    _, signature = parse_signature(signature_chunks[0].payload)
    outputs = [
        element
        for element in signature
        if element.name == "SV_Target"
        and element.semantic_index == PBR_MATERIAL_TARGET
        and element.register == PBR_MATERIAL_TARGET
    ]
    if len(outputs) != 1 or outputs[0].mask != 0xF:
        raise ContractError("authored PBR template output signature changed")
    resource_declaration = declaration_for_slot(
        words,
        OPCODE_DCL_RESOURCE,
        OPERAND_RESOURCE,
        PBR_RMAOS_SLOT,
    )
    temp_declaration, _, body = shader_declarations_and_body(words)
    temporary_count = words[temp_declaration[0] + 1]
    if temporary_count == 0 or temporary_count > 16:
        raise ContractError("authored PBR template temporary count changed")
    output_declarations: list[tuple[int, ...]] = []
    for start, end in instructions(words):
        if start >= temp_declaration[0]:
            continue
        operands = executable_operands(words, start, end)
        if any(
            operand.operand_type == OPERAND_OUTPUT
            and operand.immediate_indices == (PBR_MATERIAL_TARGET,)
            for operand in operands
        ):
            output_declarations.append(tuple(words[start:end]))
    if len(output_declarations) != 1:
        raise ContractError("authored PBR template output declaration changed")
    body_words = tuple(tuple(words[start:end]) for start, end in body[:-1])
    if not body_words or (words[body[-1][0]] & 0x7FF) != OPCODE_RET:
        raise ContractError("authored PBR template must end in one return")
    return AuthoredMaterialTemplate(
        resource_declaration=resource_declaration,
        output_declaration=output_declarations[0],
        output_signature=outputs[0],
        body=body_words,
        temporary_count=temporary_count,
    )


def patch_shader(data: bytes, template: TransformTemplate) -> bytes:
    version, chunks, shader_index, words = shader_words(data)
    all_instructions = instructions(words)
    returns = [item for item in all_instructions if (words[item[0]] & 0x7FF) == OPCODE_RET]
    if len(returns) != 1:
        raise ContractError("material shader must retain one final return")
    return_start, _ = returns[0]
    if return_start != all_instructions[-1][0]:
        raise ContractError("material shader return is not final")
    if any(
        any(
            operand.operand_type == OPERAND_OUTPUT
            and operand.immediate_indices
            and operand.immediate_indices[0]
            in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
            for operand in executable_operands(words, start, end)
        )
        for start, end in all_instructions
    ):
        raise ContractError("material shader already owns output register 6/7")

    temp_declarations = [
        item
        for item in all_instructions
        if (words[item[0]] & 0x7FF) == 0x68
    ]
    if len(temp_declarations) != 1:
        raise ContractError("material shader temporary declaration changed")
    declaration_start = temp_declarations[0][0]
    patched_words = [
        *words[:declaration_start],
        *(
            word
            for declaration in template.output_declarations
            for word in declaration
        ),
        *words[declaration_start:return_start],
        *(
            word
            for output_write in template.output_writes
            for word in output_write
        ),
        *words[return_start:],
    ]
    patched_words[1] = len(patched_words)

    output_indices = [
        index for index, chunk in enumerate(chunks) if chunk.tag == b"OSGN"
    ]
    if len(output_indices) != 1:
        raise ContractError("material shader must contain one OSGN chunk")
    output_index = output_indices[0]
    header, elements = parse_signature(chunks[output_index].payload)
    if any(
        element.name == "SV_Target"
        and (
            element.semantic_index in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
            or element.register in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
        )
        for element in elements
    ):
        raise ContractError("material output signature already owns target6/7")
    patched_chunks = list(chunks)
    patched_chunks[output_index] = DxbcChunk(
        b"OSGN",
        build_signature(header, [*elements, *template.output_signatures]),
    )
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def replace_instruction_operands(
    instruction: list[int],
    replacements: dict[int, list[int]],
) -> list[int]:
    operands = {
        operand.start: operand
        for operand in executable_operands(instruction, 0, len(instruction))
    }
    output: list[int] = []
    cursor = 0
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is None:
            output.append(instruction[cursor])
            cursor += 1
            continue
        operand = operands.get(cursor)
        if operand is None:
            raise ContractError("authored operand replacement is misaligned")
        output.extend(replacement)
        cursor = operand.end
    if len(output) > 0x7F:
        raise ContractError("authored instruction exceeds the token limit")
    output[0] = (output[0] & ~(0x7F << 24)) | (len(output) << 24)
    return output


def remap_authored_instruction(
    instruction: tuple[int, ...],
    coordinate: list[int],
    sampler: list[int],
    first_scratch: int,
    template_temporary_count: int,
) -> list[int]:
    mutable = list(instruction)
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(mutable, 0, len(mutable)):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temporary_count
            ):
                raise ContractError(
                    "authored PBR template uses an unexpected temporary"
                )
            replacements[operand.start] = replace_operand_with_temp(
                mutable,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices != (0,):
                raise ContractError(
                    "authored PBR template uses an unexpected input"
                )
            replacements[operand.start] = coordinate
        elif operand.operand_type == OPERAND_SAMPLER:
            if operand.immediate_indices != (PBR_TEMPLATE_SAMPLER_SLOT,):
                raise ContractError(
                    "authored PBR template uses an unexpected sampler"
                )
            replacements[operand.start] = sampler
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (PBR_MATERIAL_TARGET,):
                raise ContractError(
                    "authored PBR template uses an unexpected output"
                )
        elif operand.operand_type == OPERAND_RESOURCE:
            if operand.immediate_indices != (PBR_RMAOS_SLOT,):
                raise ContractError(
                    "authored PBR template uses an unexpected resource"
                )
    return replace_instruction_operands(mutable, replacements)


def patch_authored_shader(
    data: bytes,
    surface_template: TransformTemplate,
    authored_template: AuthoredMaterialTemplate,
) -> bytes | None:
    version, chunks, shader_index, words = shader_words(data)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temporary_count = words[temp_declaration[0] + 1]
    if original_temporary_count == 0 or original_temporary_count > 4090:
        raise ContractError("material shader temporary count is invalid")

    resource_declarations: dict[int, tuple[int, ...]] = {}
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != OPCODE_DCL_RESOURCE:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and len(operands[0].immediate_indices) == 1
            and operands[0].immediate_indices[0] is not None
        ):
            resource_declarations[int(operands[0].immediate_indices[0])] = (
                tuple(words[start:end])
            )
    if PBR_RMAOS_SLOT in resource_declarations:
        raise ContractError("material shader already owns t48")
    diffuse_declaration = resource_declarations.get(0)
    if diffuse_declaration is None:
        return None
    # Only a 2D base-colour sample can provide the authored RMAOS UV. Texture
    # arrays and structured resources deliberately remain on the legacy path.
    if diffuse_declaration[0] != authored_template.resource_declaration[0]:
        return None

    sample: tuple[int, int, list[Operand]] | None = None
    for start, end in body:
        if (words[start] & 0x7FF) not in SAMPLE_OPCODES:
            continue
        operands = executable_operands(words, start, end)
        if len(operands) < 4:
            continue
        if any(
            operand.operand_type == OPERAND_RESOURCE
            and operand.immediate_indices == (0,)
            for operand in operands
        ):
            sample = (start, end, operands)
            break
    if sample is None:
        return None
    sample_start, sample_end, sample_operands = sample
    coordinate = sample_operands[1]
    sampler_operands = [
        operand
        for operand in sample_operands
        if operand.operand_type == OPERAND_SAMPLER
    ]
    if len(sampler_operands) != 1:
        return None
    coordinate_words = words[coordinate.start : coordinate.end]
    sampler_words = words[
        sampler_operands[0].start : sampler_operands[0].end
    ]

    surface_declaration = surface_template.output_declarations[0]
    surface_write = surface_template.output_writes[0]
    surface_signature = surface_template.output_signatures[0]
    injected_body: list[int] = []
    for instruction in authored_template.body:
        injected_body.extend(
            remap_authored_instruction(
                instruction,
                coordinate_words,
                sampler_words,
                original_temporary_count,
                authored_template.temporary_count,
            )
        )

    rewritten_body: list[int] = []
    return_count = 0
    for start, end in body:
        opcode = words[start] & 0x7FF
        if start == sample_start and end == sample_end:
            rewritten_body.extend(words[start:end])
            rewritten_body.extend(injected_body)
        elif opcode == OPCODE_RET:
            rewritten_body.extend(surface_write)
            rewritten_body.extend(words[start:end])
            return_count += 1
        else:
            rewritten_body.extend(words[start:end])
    if return_count != 1 or (words[body[-1][0]] & 0x7FF) != OPCODE_RET:
        raise ContractError("material shader must retain one final return")

    prefix = words[: temp_declaration[0]]
    prefix.extend(surface_declaration)
    prefix.extend(authored_template.output_declaration)
    prefix.extend(authored_template.resource_declaration)
    updated_temp_declaration = words[
        temp_declaration[0] : temp_declaration[1]
    ]
    updated_temp_declaration[1] = (
        original_temporary_count + authored_template.temporary_count
    )
    patched_words = [
        *prefix,
        *updated_temp_declaration,
        *rewritten_body,
    ]
    patched_words[1] = len(patched_words)

    output_indices = [
        index for index, chunk in enumerate(chunks) if chunk.tag == b"OSGN"
    ]
    if len(output_indices) != 1:
        raise ContractError("material shader must contain one OSGN chunk")
    output_index = output_indices[0]
    header, elements = parse_signature(chunks[output_index].payload)
    if any(
        element.name == "SV_Target"
        and (
            element.semantic_index in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
            or element.register in (SURFACE_TARGET, PBR_MATERIAL_TARGET)
        )
        for element in elements
    ):
        raise ContractError("material output signature already owns target6/7")
    patched_chunks = list(chunks)
    patched_chunks[output_index] = DxbcChunk(
        b"OSGN",
        build_signature(
            header,
            [
                *elements,
                surface_signature,
                authored_template.output_signature,
            ],
        ),
    )
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def resource_paths(root: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for line in (root / "src" / "resources.rc").read_text(
        encoding="utf-8"
    ).splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) != 3 or parts[1] != "RCDATA" or not parts[2].startswith('"'):
            continue
        relative = parts[2].strip().strip('"')
        result[parts[0]] = (root / "src" / relative).resolve()
    return result


def material_contracts(root: Path) -> list[Contract]:
    manifest = json.loads(
        (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "LinearLightingContracts.json"
        ).read_text(encoding="utf-8")
    )
    if not isinstance(manifest, list) or len(manifest) != EXPECTED_MATERIAL_CONTRACTS:
        raise ContractError("surface classification requires 288 material contracts")
    paths = resource_paths(root)
    verified = (
        root / "package" / "Shaders" / "Community" / "VerifiedLinearLighting"
    )
    result: list[Contract] = []
    for index, item in enumerate(manifest):
        name = item.get("name")
        resource = item.get("resource")
        source = paths.get(resource)
        original = verified / f"{name}.dxbc"
        if not isinstance(name, str) or source is None or not source.is_file():
            raise ContractError(f"material contract {index} has no source resource")
        original_data = original.read_bytes()
        if len(original_data) < 20 or original_data[:4] != b"DXBC":
            raise ContractError(f"material contract {index} original is invalid")
        result.append(
            Contract(
                index=index,
                name=name,
                source=source,
                original_identity=(len(original_data), original_data[4:20].hex()),
            )
        )
    return result


def classify_descriptor(descriptor: int) -> int:
    if descriptor & (1 << 17):
        return SURFACE_CLASS_HAIR
    if descriptor & ((1 << 18) | (1 << 31)):
        return SURFACE_CLASS_SKIN
    if descriptor & ((1 << 5) | (1 << 9)):
        return SURFACE_CLASS_TERRAIN
    # Fallout4VR.exe 1.2.72: the DFPrePass macro builder at 0x142937540
    # emits GRASS for descriptor bit 7. SetupGeometry reads the exact per-draw
    # descriptor from geometryState+0x40; normalization through 0x142937A80
    # does not clear that bit.
    if descriptor & (1 << 7):
        return SURFACE_CLASS_GRASS
    return SURFACE_CLASS_ORDINARY


def surface_variants(
    root: Path, contracts: list[Contract]
) -> tuple[
    list[SpecializedVariant],
    list[DescriptorContract],
    list[MaterialClassContract],
    list[tuple[int, str]],
]:
    by_identity = {contract.original_identity: contract for contract in contracts}
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    descriptor_rows: list[tuple[int, int, int]] = []
    specialized: dict[tuple[int, int], SpecializedVariant] = {}
    for item in inventory.containers:
        if (
            item.family != "DFPrepass"
            or item.stage != "PS"
            or item.key is None
        ):
            continue
        contract = by_identity.get(item.identity)
        if contract is None:
            raise ContractError(
                f"DFPrepass descriptor 0x{item.key:08X} is outside material coverage"
            )
        class_code = classify_descriptor(item.key)
        descriptor_rows.append((item.key, contract.index, class_code))
        if class_code != SURFACE_CLASS_ORDINARY:
            specialized[(class_code, contract.index)] = SpecializedVariant(
                contract=contract,
                class_code=class_code,
            )

    if len(descriptor_rows) != EXPECTED_DESCRIPTOR_CONTRACTS:
        raise ContractError(
            "expected 459 exact DFPrepass descriptors, found "
            f"{len(descriptor_rows)}"
        )
    if len({row[0] for row in descriptor_rows}) != len(descriptor_rows):
        raise ContractError("DFPrepass descriptors are no longer unique")

    variants = [specialized[key] for key in sorted(specialized)]
    if len(variants) != EXPECTED_SPECIALIZED_VARIANTS:
        raise ContractError(
            "expected 106 specialized surface-class identities, found "
            f"{len(variants)}"
        )
    variant_lookup = {
        (variant.class_code, variant.contract.index): index + 1
        for index, variant in enumerate(variants)
    }
    descriptors = [
        DescriptorContract(
            descriptor=descriptor,
            contract_index=contract_index,
            class_code=class_code,
            variant_index_plus_one=variant_lookup.get(
                (class_code, contract_index), 0
            ),
        )
        for descriptor, contract_index, class_code in sorted(descriptor_rows)
    ]
    classes_by_contract = [set() for _ in contracts]
    for descriptor in descriptors:
        classes_by_contract[descriptor.contract_index].add(
            descriptor.class_code
        )
    material_classes: list[MaterialClassContract] = []
    observed_unambiguous: dict[int, int] = {}
    ambiguous_count = 0
    for contract_index, class_codes in enumerate(classes_by_contract):
        if len(class_codes) == 1:
            class_code = next(iter(class_codes))
            observed_unambiguous[class_code] = (
                observed_unambiguous.get(class_code, 0) + 1
            )
            material_classes.append(
                MaterialClassContract(
                    class_code=class_code,
                    variant_index_plus_one=variant_lookup.get(
                        (class_code, contract_index), 0
                    ),
                    grass_variant_index_plus_one=variant_lookup.get(
                        (SURFACE_CLASS_GRASS, contract_index), 0
                    ),
                )
            )
            continue
        if class_codes != {SURFACE_CLASS_ORDINARY, SURFACE_CLASS_GRASS}:
            raise ContractError(
                "material surface-class identity has an unsupported ambiguity "
                f"at contract {contract_index}: {sorted(class_codes)}"
            )
        ambiguous_count += 1
        material_classes.append(
            MaterialClassContract(
                class_code=SURFACE_CLASS_AMBIGUOUS,
                variant_index_plus_one=0,
                grass_variant_index_plus_one=variant_lookup[
                    (SURFACE_CLASS_GRASS, contract_index)
                ],
            )
        )
    if observed_unambiguous != EXPECTED_UNAMBIGUOUS_MATERIAL_CLASSES:
        raise ContractError(
            "unambiguous material surface-class census changed: "
            f"{observed_unambiguous}"
        )
    if ambiguous_count != EXPECTED_AMBIGUOUS_MATERIAL_CONTRACTS:
        raise ContractError(
            "expected 6 ordinary/grass ambiguous material identities, found "
            f"{ambiguous_count}"
        )

    vertex_shader_occurrences: dict[
        tuple[int, str], list[tuple[str, int | None]]
    ] = {}
    for item in inventory.containers:
        if item.stage != "VS":
            continue
        vertex_shader_occurrences.setdefault(item.identity, []).append(
            (item.family, item.key)
        )
    grass_vertex_rows = [
        item
        for item in inventory.containers
        if item.stage == "VS"
        and item.family == "DFPrepass"
        and item.key is not None
        and item.key & (1 << 7)
    ]
    if len(grass_vertex_rows) != EXPECTED_GRASS_VERTEX_SHADER_ROWS:
        raise ContractError(
            "expected 11 exact grass DFPrepass vertex rows, found "
            f"{len(grass_vertex_rows)}"
        )
    grass_vertex_identities = sorted(
        {item.identity for item in grass_vertex_rows}
    )
    if len(grass_vertex_identities) != EXPECTED_GRASS_VERTEX_SHADER_IDENTITIES:
        raise ContractError(
            "expected 5 exact grass vertex-shader identities, found "
            f"{len(grass_vertex_identities)}"
        )
    for shader_identity in grass_vertex_identities:
        occurrences = vertex_shader_occurrences[shader_identity]
        if any(
            family != "DFPrepass"
            or descriptor is None
            or not descriptor & (1 << 7)
            for family, descriptor in occurrences
        ):
            raise ContractError(
                "grass vertex-shader identity aliases a non-grass shader: "
                f"{shader_identity}"
            )
    return (
        variants,
        descriptors,
        material_classes,
        grass_vertex_identities,
    )


def identity(data: bytes) -> tuple[int, str]:
    if len(data) < 20 or data[:4] != b"DXBC":
        raise ContractError("generated surface-class shader is invalid")
    return len(data), data[4:20].hex()


def format_checksum(checksum: str, indent: str) -> list[str]:
    values = bytes.fromhex(checksum)
    rows = [f"{indent}{{"]
    for offset in range(0, len(values), 4):
        rows.append(
            indent
            + "    "
            + ", ".join(
                f"std::byte{{ 0x{value:02X} }}"
                for value in values[offset : offset + 4]
            )
            + ","
        )
    rows.append(f"{indent}}}")
    return rows


def render_contracts(
    contracts: list[Contract],
    variants: list[SpecializedVariant],
    descriptors: list[DescriptorContract],
    material_classes: list[MaterialClassContract],
    grass_vertex_identities: list[tuple[int, str]],
    base_data: dict[int, bytes],
    variant_data: list[bytes],
    authored_base_data: dict[int, bytes],
    authored_variant_data: dict[int, bytes],
) -> str:
    rows = [
        "// Generated by tools/generate_surface_classification_shaders.py.",
        "// Do not edit this file by hand.",
        "constexpr std::array<SurfaceClassContractDefinition, 288>",
        "    kSurfaceClassContracts{ {",
    ]
    resource = FIRST_RESOURCE_ID
    for contract in contracts:
        size, checksum = identity(base_data[contract.index])
        rows.extend(
            (
                "        {",
                f"            IDR_SURFACE_CLASS_LINEAR_{contract.index:03d}_PS,",
                f"            {size},",
            )
        )
        rows.extend(format_checksum(checksum, "            "))
        rows.extend(("        },",))
        resource += 1
    rows.extend(
        (
            "    } };",
            "",
            f"constexpr std::array<SpecializedSurfaceClassContractDefinition, {len(variants)}>",
            "    kSpecializedSurfaceClassContracts{ {",
        )
    )
    for slot, variant in enumerate(variants):
        size, checksum = identity(variant_data[slot])
        rows.extend(
            (
                "        {",
                f"            {variant.contract.index}u,",
                f"            {variant.class_code}u,",
                f"            IDR_SURFACE_CLASS_SPECIALIZED_{slot:03d}_PS,",
                f"            {size},",
            )
        )
        rows.extend(format_checksum(checksum, "            "))
        rows.extend(("        },",))
    rows.extend(
        (
            "    } };",
            "",
            "constexpr std::array<SurfaceClassContractDefinition, 288>",
            "    kAuthoredPbrSurfaceClassContracts{ {",
        )
    )
    resource = FIRST_AUTHORED_PBR_RESOURCE_ID
    for contract in contracts:
        data = authored_base_data.get(contract.index)
        if data is None:
            rows.extend(("        { 0, 0, {} },",))
            continue
        size, checksum = identity(data)
        rows.extend(
            (
                "        {",
                f"            IDR_SURFACE_CLASS_PBR_LINEAR_{contract.index:03d}_PS,",
                f"            {size},",
            )
        )
        rows.extend(format_checksum(checksum, "            "))
        rows.extend(("        },",))
        resource += 1
    rows.extend(
        (
            "    } };",
            "",
            f"constexpr std::array<SpecializedSurfaceClassContractDefinition, {len(variants)}>",
            "    kAuthoredPbrSpecializedSurfaceClassContracts{ {",
        )
    )
    for slot, variant in enumerate(variants):
        data = authored_variant_data.get(slot)
        if data is None:
            rows.extend(
                (
                    "        {",
                    f"            {variant.contract.index}u,",
                    f"            {variant.class_code}u,",
                    "            0,",
                    "            0,",
                    "            {},",
                    "        },",
                )
            )
            continue
        size, checksum = identity(data)
        rows.extend(
            (
                "        {",
                f"            {variant.contract.index}u,",
                f"            {variant.class_code}u,",
                f"            IDR_SURFACE_CLASS_PBR_SPECIALIZED_{slot:03d}_PS,",
                f"            {size},",
            )
        )
        rows.extend(format_checksum(checksum, "            "))
        rows.extend(("        },",))
        resource += 1
    rows.extend(
        (
            "    } };",
            "",
            "constexpr std::array<SurfaceClassMaterialContract, 288>",
            "    kSurfaceClassMaterialContracts{ {",
        )
    )
    for contract in material_classes:
        rows.extend(
            (
                "        {",
                f"            {contract.class_code}u,",
                f"            {contract.variant_index_plus_one}u,",
                f"            {contract.grass_variant_index_plus_one}u,",
                "        },",
            )
        )
    rows.extend(
        (
            "    } };",
            "",
            "constexpr std::array<DxbcIdentity, 5>",
            "    kGrassVertexShaderIdentities{ {",
        )
    )
    for size, checksum in grass_vertex_identities:
        rows.extend(("        {", f"            {size},"))
        rows.extend(format_checksum(checksum, "            "))
        rows.extend(("        },",))
    rows.extend(
        (
            "    } };",
            "",
            "constexpr std::array<SurfaceClassDescriptorContract, 459>",
            "    kSurfaceClassDescriptorContracts{ {",
        )
    )
    for descriptor in descriptors:
        rows.extend(
            (
                "        {",
                f"            0x{descriptor.descriptor:08X}u,",
                f"            {descriptor.contract_index}u,",
                f"            {descriptor.class_code}u,",
                f"            {descriptor.variant_index_plus_one}u,",
                "        },",
            )
        )
    rows.extend(("    } };", ""))
    return "\n".join(rows)


def render_resource_header(
    contracts: list[Contract],
    variants: list[SpecializedVariant],
    authored_base_data: dict[int, bytes],
    authored_variant_data: dict[int, bytes],
) -> str:
    rows = [
        "#pragma once",
        "",
        "// Generated by tools/generate_surface_classification_shaders.py.",
    ]
    resource = FIRST_RESOURCE_ID
    for contract in contracts:
        rows.append(
            f"#define IDR_SURFACE_CLASS_LINEAR_{contract.index:03d}_PS {resource}"
        )
        resource += 1
    for slot, _ in enumerate(variants):
        rows.append(
            f"#define IDR_SURFACE_CLASS_SPECIALIZED_{slot:03d}_PS {resource}"
        )
        resource += 1
    resource = FIRST_AUTHORED_PBR_RESOURCE_ID
    for contract in contracts:
        if contract.index not in authored_base_data:
            continue
        rows.append(
            f"#define IDR_SURFACE_CLASS_PBR_LINEAR_{contract.index:03d}_PS {resource}"
        )
        resource += 1
    for slot, _ in enumerate(variants):
        if slot not in authored_variant_data:
            continue
        rows.append(
            f"#define IDR_SURFACE_CLASS_PBR_SPECIALIZED_{slot:03d}_PS {resource}"
        )
        resource += 1
    rows.append("")
    return "\n".join(rows)


def render_resource_script(
    contracts: list[Contract],
    variants: list[SpecializedVariant],
    authored_base_data: dict[int, bytes],
    authored_variant_data: dict[int, bytes],
) -> str:
    rows = ["// Generated by tools/generate_surface_classification_shaders.py."]
    for contract in contracts:
        rows.append(
            f'IDR_SURFACE_CLASS_LINEAR_{contract.index:03d}_PS RCDATA "../package/Shaders/Community/SurfaceClassification/Linear/{contract.name}.dxbc"'
        )
    for slot, variant in enumerate(variants):
        folder = SURFACE_CLASS_NAMES[variant.class_code]
        rows.append(
            f'IDR_SURFACE_CLASS_SPECIALIZED_{slot:03d}_PS RCDATA "../package/Shaders/Community/SurfaceClassification/{folder}/{variant.contract.name}.dxbc"'
        )
    for contract in contracts:
        if contract.index not in authored_base_data:
            continue
        rows.append(
            f'IDR_SURFACE_CLASS_PBR_LINEAR_{contract.index:03d}_PS RCDATA "../package/Shaders/Community/SurfaceClassification/PBR/Linear/{contract.name}.dxbc"'
        )
    for slot, variant in enumerate(variants):
        if slot not in authored_variant_data:
            continue
        folder = SURFACE_CLASS_NAMES[variant.class_code]
        rows.append(
            f'IDR_SURFACE_CLASS_PBR_SPECIALIZED_{slot:03d}_PS RCDATA "../package/Shaders/Community/SurfaceClassification/PBR/{folder}/{variant.contract.name}.dxbc"'
        )
    rows.append("")
    return "\n".join(rows)


def compare_or_write(path: Path, data: bytes | str, check: bool) -> None:
    encoded = data.encode("utf-8") if isinstance(data, str) else data
    if check:
        if not path.is_file() or path.read_bytes() != encoded:
            raise ContractError(f"generated surface-class artifact is stale: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def main() -> int:
    args = arguments()
    if args.check == args.write_assets:
        raise ContractError("select exactly one of --check or --write-assets")
    root = args.root.resolve()
    contracts = material_contracts(root)
    (
        variants,
        descriptors,
        material_classes,
        grass_vertex_identities,
    ) = surface_variants(root, contracts)
    with tempfile.TemporaryDirectory(prefix="fo4vr-surface-class-") as folder:
        temporary = Path(folder)
        required_class_codes = {
            SURFACE_CLASS_ORDINARY,
            *(variant.class_code for variant in variants),
        }
        templates = {
            class_code: template_contract(
                compile_template(
                    root, args.fxc.resolve(), temporary, class_code
                )
            )
            for class_code in sorted(required_class_codes)
        }
        authored_template = authored_template_contract(
            compile_authored_template(
                root,
                args.fxc.resolve(),
                temporary,
            )
        )
        base_data = {
            contract.index: patch_shader(
                contract.source.read_bytes(), templates[SURFACE_CLASS_ORDINARY]
            )
            for contract in contracts
        }
        variant_data = [
            patch_shader(
                variant.contract.source.read_bytes(),
                templates[variant.class_code],
            )
            for variant in variants
        ]
        authored_base_data: dict[int, bytes] = {}
        for contract in contracts:
            candidate = patch_authored_shader(
                contract.source.read_bytes(),
                templates[SURFACE_CLASS_ORDINARY],
                authored_template,
            )
            if candidate is not None:
                authored_base_data[contract.index] = candidate
        authored_variant_data: dict[int, bytes] = {}
        for slot, variant in enumerate(variants):
            candidate = patch_authored_shader(
                variant.contract.source.read_bytes(),
                templates[variant.class_code],
                authored_template,
            )
            if candidate is not None:
                authored_variant_data[slot] = candidate
        if len(authored_base_data) != EXPECTED_AUTHORED_PBR_CONTRACTS:
            raise ContractError(
                "authored PBR 2D material coverage changed: "
                f"{len(authored_base_data)}"
            )
        if (
            len(authored_variant_data)
            != EXPECTED_AUTHORED_PBR_SPECIALIZED_VARIANTS
        ):
            raise ContractError(
                "authored PBR specialized coverage changed: "
                f"{len(authored_variant_data)}"
            )

    package = (
        root / "package" / "Shaders" / "Community" / "SurfaceClassification"
    )
    expected_assets = {
        package / "Linear" / f"{contract.name}.dxbc"
        for contract in contracts
    }
    expected_assets.update(
        package
        / SURFACE_CLASS_NAMES[variant.class_code]
        / f"{variant.contract.name}.dxbc"
        for variant in variants
    )
    expected_assets.update(
        package / "PBR" / "Linear" / f"{contract.name}.dxbc"
        for contract in contracts
        if contract.index in authored_base_data
    )
    expected_assets.update(
        package
        / "PBR"
        / SURFACE_CLASS_NAMES[variant.class_code]
        / f"{variant.contract.name}.dxbc"
        for slot, variant in enumerate(variants)
        if slot in authored_variant_data
    )
    existing_assets = set(package.rglob("*.dxbc"))
    stale_assets = sorted(existing_assets - expected_assets)
    if args.check and stale_assets:
        raise ContractError(
            "stale generated surface-class assets remain: "
            + ", ".join(str(path.relative_to(root)) for path in stale_assets)
        )
    if args.write_assets:
        for path in stale_assets:
            path.unlink()
    for contract in contracts:
        compare_or_write(
            package / "Linear" / f"{contract.name}.dxbc",
            base_data[contract.index],
            args.check,
        )
    for slot, variant in enumerate(variants):
        folder = SURFACE_CLASS_NAMES[variant.class_code]
        compare_or_write(
            package / folder / f"{variant.contract.name}.dxbc",
            variant_data[slot],
            args.check,
        )
    for contract in contracts:
        data = authored_base_data.get(contract.index)
        if data is None:
            continue
        compare_or_write(
            package / "PBR" / "Linear" / f"{contract.name}.dxbc",
            data,
            args.check,
        )
    for slot, variant in enumerate(variants):
        data = authored_variant_data.get(slot)
        if data is None:
            continue
        compare_or_write(
            package
            / "PBR"
            / SURFACE_CLASS_NAMES[variant.class_code]
            / f"{variant.contract.name}.dxbc",
            data,
            args.check,
        )
    compare_or_write(
        root
        / "src"
        / "Features"
        / "surface_classification"
        / "GeneratedSurfaceClassContracts.inl",
        render_contracts(
            contracts,
            variants,
            descriptors,
            material_classes,
            grass_vertex_identities,
            base_data,
            variant_data,
            authored_base_data,
            authored_variant_data,
        ),
        args.check,
    )
    compare_or_write(
        root / "src" / "GeneratedSurfaceClassificationResources.h",
        render_resource_header(
            contracts,
            variants,
            authored_base_data,
            authored_variant_data,
        ),
        args.check,
    )
    compare_or_write(
        root / "src" / "GeneratedSurfaceClassificationResources.rc",
        render_resource_script(
            contracts,
            variants,
            authored_base_data,
            authored_variant_data,
        ),
        args.check,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"surface-class generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
