from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_complex_material_producer_contracts as generator  # noqa: E402


class ComplexMaterialProducerGeneratorTests(unittest.TestCase):
    def test_six_bit_material_type_does_not_alias_type_33(self) -> None:
        descriptor = 0x00002102
        self.assertEqual(generator.material_type_from_descriptor(descriptor), 33)
        self.assertEqual((descriptor >> 8) & 0x1F, 1)

    def test_descriptor_domain_is_uint32(self) -> None:
        with self.assertRaises(generator.ContractError):
            generator.material_type_from_descriptor(-1)
        with self.assertRaises(generator.ContractError):
            generator.material_type_from_descriptor(0x1_0000_0000)

    def test_stale_generated_output_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "Generated.inl"
            output.write_text("stale\n", encoding="utf-8")
            with self.assertRaises(generator.ContractError):
                generator.write_or_check(output, "current\n", True)

    def test_missing_generated_output_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "Generated.inl"
            with self.assertRaises(generator.ContractError):
                generator.write_or_check(output, "current\n", True)


if __name__ == "__main__":
    unittest.main()
