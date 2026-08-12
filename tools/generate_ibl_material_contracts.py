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
VANILLA_ENVIRONMENT_SLOT = 8
PUBLISHED_ENVIRONMENT_SLOT = 30
PUBLISHED_VALIDITY_SLOT = 31
ENVIRONMENT_SAMPLER_SLOT = 8
FIRST_RESOURCE_ID = 1054

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
        "PublishedEnvironment : register(t30)",
        "PublishedValidity : register(t31)",
        "EnvironmentSampler : register(s8)",
        "IblMaterialConstants : register(b5)",
        "saturate(validity * IblWeight)",
        "lerp(vanilla.xyz, published, weight)",
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
        "dcl_sampler s8, mode_default",
        "dcl_resource_texturecubearray (float,float,float,float) t8",
        "dcl_resource_texturecube (float,float,float,float) t30",
        "dcl_resource_texturecube (float,float,float,float) t31",
        "dcl_temps 2",
        "mul_sat r0.x, r0.x, cb5[0].x",
    ):
        if required not in text:
            raise ContractError(
                "IBL material template assembly changed: " + required
            )
    return output.read_bytes()


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
) -> tuple[list[list[int]], list[list[int]]]:
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
    ]
    _, _, body = shader_declarations_and_body(words)
    body_words = [words[start:end] for start, end in body]
    opcodes = [instruction[0] & 0x7FF for instruction in body_words]
    expected = [
        SAMPLE_L_OPCODE,
        MUL_OPCODE,
        SAMPLE_L_OPCODE,
        SAMPLE_L_OPCODE,
        ADD_OPCODE,
        MAD_OPCODE,
        MOV_OPCODE,
        OPCODE_RET,
    ]
    if opcodes != expected:
        raise ContractError(
            "IBL material template instruction sequence changed: "
            + ", ".join(f"{opcode:#x}" for opcode in opcodes)
        )
    return declarations, body_words[:6]


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


def remap_template_instruction(
    instruction: list[int],
    coordinate: list[int],
    lod: list[int],
    first_scratch: int,
    second_scratch: int,
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in executable_operands(instruction, 0, len(instruction)):
        if operand.operand_type == OPERAND_TEMP:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction,
                    operand,
                    first_scratch,
                )
            elif operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction,
                    operand,
                    second_scratch,
                )
            else:
                raise ContractError(
                    "IBL material template uses an unexpected temporary"
                )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = coordinate
            elif operand.immediate_indices == (1,):
                replacements[operand.start] = lod
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
                second_scratch,
            )
    return replace_instruction_operands(instruction, replacements)


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
) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temp_count = words[temp_declaration[0] + 1]
    if original_temp_count == 0 or original_temp_count > 4093:
        raise ContractError("DFComposite temporary-register count is invalid")

    occupied_buffers: set[int] = set()
    occupied_resources: set[int] = set()
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
            occupied_buffers.add(int(operands[0].immediate_indices[0]))
        else:
            occupied_resources.add(int(operands[0].immediate_indices[0]))
    if IBL_CONSTANT_SLOT in occupied_buffers:
        raise ContractError("DFComposite unexpectedly owns b5")
    if {
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
    } & occupied_resources:
        raise ContractError("DFComposite unexpectedly owns t30 or t31")

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

    first_scratch = original_temp_count
    second_scratch = original_temp_count + 1
    replacement: list[int] = []
    for instruction in transform:
        replacement.extend(
            remap_template_instruction(
                instruction,
                coordinate_words,
                lod_words,
                first_scratch,
                second_scratch,
            )
        )
    replacement.extend(
        final_material_move(destination_words, second_scratch)
    )

    prefix = words[: temp_declaration[0]]
    for declaration in declarations:
        prefix.extend(declaration)
    updated_temp_declaration = words[temp_declaration[0] : temp_declaration[1]]
    updated_temp_declaration[1] = original_temp_count + 2

    rewritten_body: list[int] = []
    for start, end in body:
        if start == sample_start and end == sample_end:
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
    if candidate_buffers != original_buffers:
        raise ContractError(f"{name} changed vanilla constant buffers")
    original_textures = set(original_declarations.textures)
    candidate_textures = set(candidate_declarations.textures)
    if candidate_textures - {
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
    } != original_textures or not {
        PUBLISHED_ENVIRONMENT_SLOT,
        PUBLISHED_VALIDITY_SLOT,
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
        "dcl_resource_texturecube (float,float,float,float) t30",
        "dcl_resource_texturecube (float,float,float,float) t31",
    ):
        if declaration not in candidate_text:
            raise ContractError(f"{name} is missing {declaration}")
    if len(re.findall(r"\bt30(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t30 exactly once")
    if len(re.findall(r"\bt31(?:\b|\.)", candidate_text)) != 2:
        raise ContractError(f"{name} must declare and sample t31 exactly once")
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
        contracts = parse_contracts(root)
        originals = exact_originals(root, contracts)
        fxc = census.find_fxc(None)
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_cs_ibl_material_"
        ) as directory:
            temporary = Path(directory)
            template = compile_template(root, fxc, temporary)
            declarations, transform = template_contract(template)
            candidates: list[bytes] = []
            for index, original in enumerate(originals):
                name = contract_name(index, original.checksum)
                candidate = patch_shader(
                    original.data,
                    declarations,
                    transform,
                )
                validate_candidate(
                    fxc,
                    name,
                    original.data,
                    candidate,
                    temporary,
                )
                candidates.append(candidate)

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
            "IBL material contracts verified: 41 exact DFComposite identities; "
            "vanilla t8/s8 fallback plus validity-weighted t30/t31/b5 consumption."
        )
    except (OSError, ContractError, census.CensusError) as error:
        print(f"IBL material contract generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
