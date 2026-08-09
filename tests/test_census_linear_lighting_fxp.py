from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1] / "tools" / "census_linear_lighting_fxp.py"
)
SPEC = importlib.util.spec_from_file_location("census_linear_lighting_fxp", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not load census module from {MODULE_PATH}")
CENSUS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CENSUS
SPEC.loader.exec_module(CENSUS)


PROGRAM_TYPES = {
    "PS": 0,
    "VS": 1,
    "HS": 3,
    "DS": 4,
    "CS": 5,
}


def make_dxbc(stage: str) -> bytes:
    shader_payload = struct.pack("<II", PROGRAM_TYPES[stage] << 16, 2)
    shader_chunk = b"SHDR" + struct.pack("<I", len(shader_payload)) + shader_payload
    chunk_offset = 0x24
    total_size = chunk_offset + len(shader_chunk)
    return b"".join(
        (
            b"DXBC",
            bytes(16),
            struct.pack("<III", 1, total_size, 1),
            struct.pack("<I", chunk_offset),
            shader_chunk,
        )
    )


def make_record(stage: str, key: int, payload_stage: str | None = None) -> bytes:
    payload = make_dxbc(payload_stage or stage)
    header_size = dict(CENSUS.FXP_STAGE_LAYOUT)[stage]
    header = bytearray(header_size)
    struct.pack_into("<III", header, 0, CENSUS.FXP_RECORD_MAGIC, len(payload), key)
    return bytes(header) + payload


def make_fxp(
    first_family_records: dict[str, list[tuple[int | None, str | None]]] | None = None,
    fixed_counts_override: tuple[int, int, int, int, int] | None = None,
) -> bytes:
    records = first_family_records or {}
    output = bytearray()
    for family_index, _ in enumerate(CENSUS.FXP_FAMILIES):
        stage_records = records if family_index == 0 else {}
        output.extend(
            struct.pack(
                "<5I",
                *(len(stage_records.get(stage, [])) for stage, _ in CENSUS.FXP_STAGE_LAYOUT),
            )
        )
        for stage, _ in CENSUS.FXP_STAGE_LAYOUT:
            for key, payload_stage in stage_records.get(stage, []):
                if key is None:
                    output.extend(struct.pack("<I", CENSUS.FXP_NULL_RECORD_MAGIC))
                else:
                    output.extend(make_record(stage, key, payload_stage))
    for _ in CENSUS.FXP_COMPUTE_FAMILIES:
        output.extend(struct.pack("<5I", 0, 0, 0, 0, 0))
    for _ in range(CENSUS.FXP_IMAGE_SPACE_BLOCK_COUNT):
        output.extend(struct.pack("<5I", 0, 0, 0, 0, 0))
    for fixed_index, (_, expected_counts) in enumerate(
        CENSUS.FXP_FIXED_BUILTIN_LAYOUT
    ):
        counts = (
            fixed_counts_override
            if fixed_index == 0 and fixed_counts_override is not None
            else expected_counts
        )
        output.extend(struct.pack("<5I", *counts))
        for (stage, _), count in zip(CENSUS.FXP_STAGE_LAYOUT, counts):
            for key in range(count):
                output.extend(make_record(stage, key + 1))
    for _ in CENSUS.FXP_FINAL_COMPUTE_FAMILIES:
        output.extend(struct.pack("<5I", 0, 0, 0, 0, 0))
    return bytes(output)


class FxpParserTests(unittest.TestCase):
    def test_decodes_family_stage_key_and_record_layout(self) -> None:
        data = make_fxp(
            {
                stage: [(index + 1, None)]
                for index, (stage, _) in enumerate(CENSUS.FXP_STAGE_LAYOUT)
            }
        )

        inventory = CENSUS.parse_fxp(data)
        containers = tuple(
            container
            for container in inventory.containers
            if container.family == CENSUS.FXP_FAMILIES[0]
        )

        self.assertEqual(len(containers), 5)
        self.assertEqual(
            [container.family for container in containers],
            [CENSUS.FXP_FAMILIES[0]] * 5,
        )
        self.assertEqual(
            [container.stage for container in containers],
            [stage for stage, _ in CENSUS.FXP_STAGE_LAYOUT],
        )
        self.assertEqual([container.key for container in containers], [1, 2, 3, 4, 5])
        self.assertTrue(
            all(container.stage == container.shader_type for container in containers)
        )
        family_slots = tuple(
            slot
            for slot in inventory.slots
            if slot.family == CENSUS.FXP_FAMILIES[0]
        )
        self.assertEqual(len(family_slots), 5)
        self.assertFalse(any(slot.is_null for slot in family_slots))

    def test_preserves_explicit_null_slots(self) -> None:
        data = make_fxp({"VS": [(None, None)]})

        inventory = CENSUS.parse_fxp(data)

        family_containers = tuple(
            container
            for container in inventory.containers
            if container.family == CENSUS.FXP_FAMILIES[0]
        )
        family_slots = tuple(
            slot
            for slot in inventory.slots
            if slot.family == CENSUS.FXP_FAMILIES[0]
        )
        self.assertEqual(family_containers, ())
        self.assertEqual(len(family_slots), 1)
        self.assertTrue(family_slots[0].is_null)
        self.assertEqual(family_slots[0].record_offset, 20)

    def test_rejects_bad_record_magic_without_scanning_forward(self) -> None:
        data = bytearray(make_fxp({"VS": [(1, None)]}))
        struct.pack_into("<I", data, 20, 0)

        with self.assertRaisesRegex(CENSUS.CensusError, "has magic"):
            CENSUS.parse_fxp(bytes(data))

    def test_rejects_declared_size_mismatch(self) -> None:
        data = bytearray(make_fxp({"VS": [(1, None)]}))
        struct.pack_into("<I", data, 24, len(make_dxbc("VS")) + 4)

        with self.assertRaisesRegex(CENSUS.CensusError, "DXBC bytes"):
            CENSUS.parse_fxp(bytes(data))

    def test_rejects_stage_mismatch(self) -> None:
        data = make_fxp({"VS": [(1, "PS")]})

        with self.assertRaisesRegex(CENSUS.CensusError, "contains a PS program"):
            CENSUS.parse_fxp(data)

    def test_rejects_duplicate_keys_within_family_stage(self) -> None:
        data = make_fxp({"VS": [(7, None), (7, None)]})

        with self.assertRaisesRegex(CENSUS.CensusError, "duplicate key"):
            CENSUS.parse_fxp(data)

    def test_rejects_trailing_bytes(self) -> None:
        data = make_fxp() + b"trailing"

        with self.assertRaisesRegex(CENSUS.CensusError, "trailing bytes after"):
            CENSUS.parse_fxp(data)

    def test_rejects_wrong_fixed_builtin_counts(self) -> None:
        data = make_fxp(fixed_counts_override=(0, 0, 0, 0, 0))

        with self.assertRaisesRegex(CENSUS.CensusError, "expected"):
            CENSUS.parse_fxp(data)

    def test_rejects_impossible_count_before_looping(self) -> None:
        data = bytearray(make_fxp())
        struct.pack_into("<I", data, 0, CENSUS.FXP_MAX_RECORDS + 1)

        with self.assertRaisesRegex(CENSUS.CensusError, "record limit"):
            CENSUS.parse_fxp(bytes(data))


if __name__ == "__main__":
    unittest.main()
