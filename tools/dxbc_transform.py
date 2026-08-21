from __future__ import annotations

import struct
from dataclasses import dataclass


class TransformError(RuntimeError):
    pass


DXBC_HEADER_SIZE = 32
DXBC_CHECKSUM_OFFSET = 4
DXBC_PAYLOAD_OFFSET = 20
DXBC_SIZE_OFFSET = 24
DXBC_CHUNK_COUNT_OFFSET = 28
DXBC_CHUNK_OFFSETS_OFFSET = 32

OPCODE_DCL_CONSTANT_BUFFER = 0x59
OPCODE_DCL_RESOURCE = 0x58
OPCODE_DCL_TEMPS = 0x68
OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW = 0x9D
OPCODE_CUSTOMDATA = 0x35
OPCODE_RET = 0x3E

OPERAND_TEMP = 0
OPERAND_INPUT = 1
OPERAND_OUTPUT = 2
OPERAND_IMMEDIATE32 = 4
OPERAND_SAMPLER = 6
OPERAND_RESOURCE = 7
OPERAND_CONSTANT_BUFFER = 8
OPERAND_UNORDERED_ACCESS_VIEW = 30


@dataclass(frozen=True)
class DxbcChunk:
    tag: bytes
    payload: bytes


@dataclass(frozen=True)
class Operand:
    start: int
    end: int
    operand_type: int
    immediate_indices: tuple[int | None, ...]
    relative_operands: tuple[Operand, ...] = ()


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def pack_words(words: list[int]) -> bytes:
    return struct.pack(f"<{len(words)}I", *words)


def parse_dxbc(data: bytes) -> tuple[int, list[DxbcChunk]]:
    if len(data) < DXBC_HEADER_SIZE or data[:4] != b"DXBC":
        raise TransformError("input is not a DXBC container")
    if u32(data, DXBC_SIZE_OFFSET) != len(data):
        raise TransformError("DXBC declared size does not match its bytes")
    version = u32(data, 20)
    chunk_count = u32(data, DXBC_CHUNK_COUNT_OFFSET)
    header_size = DXBC_CHUNK_OFFSETS_OFFSET + chunk_count * 4
    if chunk_count == 0 or header_size > len(data):
        raise TransformError("DXBC chunk table is invalid")

    chunks: list[DxbcChunk] = []
    occupied: list[tuple[int, int]] = []
    for index in range(chunk_count):
        offset = u32(data, DXBC_CHUNK_OFFSETS_OFFSET + index * 4)
        if offset < header_size or offset + 8 > len(data):
            raise TransformError("DXBC chunk offset is invalid")
        size = u32(data, offset + 4)
        end = offset + 8 + size
        if end > len(data):
            raise TransformError("DXBC chunk escapes its container")
        occupied.append((offset, end))
        chunks.append(DxbcChunk(data[offset : offset + 4], data[offset + 8 : end]))
    ordered = sorted(occupied)
    for (_, previous_end), (current_start, _) in zip(ordered, ordered[1:]):
        if current_start < previous_end:
            raise TransformError("DXBC chunks overlap")
    return version, chunks


ROTATION_COUNTS = (
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
)
ROUND_CONSTANTS = (
    0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE,
    0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
    0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE,
    0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
    0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA,
    0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
    0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED,
    0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
    0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C,
    0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
    0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05,
    0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
    0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039,
    0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
    0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1,
    0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391,
)


def rotate_left(value: int, count: int) -> int:
    return ((value << count) | (value >> (32 - count))) & 0xFFFFFFFF


def checksum_transform(state: list[int], block: bytes) -> None:
    words = struct.unpack("<16I", block)
    a, b, c, d = state
    for index in range(64):
        if index < 16:
            function = (b & c) | ((~b) & d)
            word_index = index
        elif index < 32:
            function = (d & b) | ((~d) & c)
            word_index = (5 * index + 1) % 16
        elif index < 48:
            function = b ^ c ^ d
            word_index = (3 * index + 5) % 16
        else:
            function = c ^ (b | (~d))
            word_index = (7 * index) % 16
        previous_d = d
        d = c
        c = b
        value = (a + function + ROUND_CONSTANTS[index] + words[word_index]) & 0xFFFFFFFF
        b = (b + rotate_left(value, ROTATION_COUNTS[index])) & 0xFFFFFFFF
        a = previous_d
    state[0] = (state[0] + a) & 0xFFFFFFFF
    state[1] = (state[1] + b) & 0xFFFFFFFF
    state[2] = (state[2] + c) & 0xFFFFFFFF
    state[3] = (state[3] + d) & 0xFFFFFFFF


def compute_dxbc_checksum(payload: bytes) -> bytes:
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    complete_blocks = len(payload) // 64
    for index in range(complete_blocks):
        checksum_transform(state, payload[index * 64 : (index + 1) * 64])

    remainder = payload[complete_blocks * 64 :]
    final_block = bytearray(64)
    if len(remainder) < 56:
        struct.pack_into("<I", final_block, 0, len(payload) * 8)
        final_block[4 : 4 + len(remainder)] = remainder
        final_block[4 + len(remainder)] = 0x80
    else:
        final_block[: len(remainder)] = remainder
        final_block[len(remainder)] = 0x80
        checksum_transform(state, bytes(final_block))
        final_block = bytearray(64)
        struct.pack_into("<I", final_block, 0, len(payload) * 8)
    struct.pack_into("<I", final_block, 60, len(payload) * 2 | 1)
    checksum_transform(state, bytes(final_block))
    return struct.pack("<4I", *state)


def build_dxbc(version: int, chunks: list[DxbcChunk]) -> bytes:
    header_size = DXBC_CHUNK_OFFSETS_OFFSET + len(chunks) * 4
    output = bytearray(header_size)
    output[:4] = b"DXBC"
    struct.pack_into("<I", output, 20, version)
    struct.pack_into("<I", output, DXBC_CHUNK_COUNT_OFFSET, len(chunks))
    offsets: list[int] = []
    for chunk in chunks:
        while len(output) % 4:
            output.append(0)
        offsets.append(len(output))
        output.extend(chunk.tag)
        output.extend(struct.pack("<I", len(chunk.payload)))
        output.extend(chunk.payload)
    for index, offset in enumerate(offsets):
        struct.pack_into("<I", output, DXBC_CHUNK_OFFSETS_OFFSET + index * 4, offset)
    struct.pack_into("<I", output, DXBC_SIZE_OFFSET, len(output))
    output[DXBC_CHECKSUM_OFFSET : DXBC_CHECKSUM_OFFSET + 16] = compute_dxbc_checksum(
        bytes(output[DXBC_PAYLOAD_OFFSET:])
    )
    return bytes(output)


def shader_words(data: bytes) -> tuple[int, list[DxbcChunk], int, list[int]]:
    version, chunks = parse_dxbc(data)
    shader_indices = [
        index for index, chunk in enumerate(chunks) if chunk.tag in (b"SHEX", b"SHDR")
    ]
    if len(shader_indices) != 1:
        raise TransformError("DXBC must contain exactly one shader program")
    shader_index = shader_indices[0]
    payload = chunks[shader_index].payload
    if len(payload) < 8 or len(payload) % 4:
        raise TransformError("DXBC shader program is truncated")
    words = list(struct.unpack(f"<{len(payload) // 4}I", payload))
    if words[1] != len(words):
        raise TransformError("DXBC shader token length is invalid")
    return version, chunks, shader_index, words


def instructions(words: list[int]) -> list[tuple[int, int]]:
    result: list[tuple[int, int]] = []
    cursor = 2
    while cursor < len(words):
        opcode = words[cursor] & 0x7FF
        length = (words[cursor] >> 24) & 0x7F
        if opcode == OPCODE_CUSTOMDATA:
            if cursor + 1 >= len(words):
                raise TransformError("DXBC custom-data instruction is truncated")
            length = words[cursor + 1]
        if length == 0 or cursor + length > len(words):
            raise TransformError("DXBC instruction length is invalid")
        result.append((cursor, cursor + length))
        cursor += length
    if cursor != len(words):
        raise TransformError("DXBC instruction stream did not terminate exactly")
    return result


def opcode_extension_end(words: list[int], start: int, end: int) -> int:
    cursor = start + 1
    extended = (words[start] & 0x80000000) != 0
    while extended:
        if cursor >= end:
            raise TransformError("DXBC extended opcode is truncated")
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1
    return cursor


def parse_operand(words: list[int], start: int, end: int) -> Operand:
    if start >= end:
        raise TransformError("DXBC operand is truncated")
    token = words[start]
    operand_type = (token >> 12) & 0xFF
    component_count = token & 0x3
    index_dimension = (token >> 20) & 0x3
    cursor = start + 1

    extended = (token & 0x80000000) != 0
    while extended:
        if cursor >= end:
            raise TransformError("DXBC extended operand is truncated")
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1

    if operand_type == OPERAND_IMMEDIATE32:
        if index_dimension != 0:
            raise TransformError("DXBC immediate operand has indices")
        if component_count == 1:
            cursor += 1
        elif component_count == 2:
            cursor += 4
        elif component_count == 3:
            if cursor >= end:
                raise TransformError("DXBC N-component immediate is truncated")
            count = words[cursor]
            cursor += 1 + count
        if cursor > end:
            raise TransformError("DXBC immediate operand escapes its instruction")
        return Operand(start, cursor, operand_type, (), ())

    indices: list[int | None] = []
    relative_operands: list[Operand] = []
    for dimension in range(index_dimension):
        representation = (token >> (22 + dimension * 3)) & 0x7
        if representation == 0:
            if cursor >= end:
                raise TransformError("DXBC immediate index is truncated")
            indices.append(words[cursor])
            cursor += 1
        elif representation == 1:
            if cursor + 2 > end:
                raise TransformError("DXBC 64-bit index is truncated")
            indices.append(None)
            cursor += 2
        elif representation == 2:
            relative = parse_operand(words, cursor, end)
            indices.append(None)
            relative_operands.append(relative)
            cursor = relative.end
        elif representation in (3, 4):
            immediate_words = 1 if representation == 3 else 2
            if cursor + immediate_words > end:
                raise TransformError("DXBC relative base index is truncated")
            cursor += immediate_words
            relative = parse_operand(words, cursor, end)
            indices.append(None)
            relative_operands.append(relative)
            cursor = relative.end
        else:
            raise TransformError("DXBC operand uses an unsupported index representation")
    return Operand(
        start,
        cursor,
        operand_type,
        tuple(indices),
        tuple(relative_operands),
    )


def flatten_operands(operands: list[Operand]) -> list[Operand]:
    result: list[Operand] = []

    def append_tree(operand: Operand) -> None:
        result.append(operand)
        for relative in operand.relative_operands:
            append_tree(relative)

    for operand in operands:
        append_tree(operand)
    return result


def executable_operands(
    words: list[int], instruction_start: int, instruction_end: int
) -> list[Operand]:
    cursor = opcode_extension_end(words, instruction_start, instruction_end)
    result: list[Operand] = []
    while cursor < instruction_end:
        operand = parse_operand(words, cursor, instruction_end)
        if operand.end <= cursor:
            raise TransformError("DXBC operand parser made no progress")
        result.append(operand)
        cursor = operand.end
    if cursor != instruction_end:
        raise TransformError("DXBC operands did not fill their instruction")
    return result


def shader_declarations_and_body(
    words: list[int],
) -> tuple[tuple[int, int], int, list[tuple[int, int]]]:
    all_instructions = instructions(words)
    temp_declarations = [
        item for item in all_instructions if (words[item[0]] & 0x7FF) == OPCODE_DCL_TEMPS
    ]
    if len(temp_declarations) != 1:
        raise TransformError("DXBC shader must declare one temporary-register block")
    temp_declaration = temp_declarations[0]
    if temp_declaration[1] - temp_declaration[0] != 2:
        raise TransformError("DXBC temporary-register declaration changed shape")
    executable_start = temp_declaration[1]
    body = [item for item in all_instructions if item[0] >= executable_start]
    if not body or body[0][0] != executable_start:
        raise TransformError("DXBC declarations follow dcl_temps")
    return temp_declaration, executable_start, body


def replace_operand_with_temp(
    words: list[int], operand: Operand, register: int
) -> list[int]:
    if register < 0:
        raise TransformError("temporary register is invalid")
    token = words[operand.start]
    token &= ~(0xFF << 12)
    token |= OPERAND_TEMP << 12
    token &= ~(0x3 << 20)
    token |= 1 << 20
    token &= ~(0x3F << 22)

    cursor = operand.start + 1
    extended = (words[operand.start] & 0x80000000) != 0
    while extended:
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1
    return [token, *words[operand.start + 1 : cursor], register]
