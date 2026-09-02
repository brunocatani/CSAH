from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "build_authored_pbr_pack.py"
SPEC = importlib.util.spec_from_file_location("build_authored_pbr_pack", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def write_image(path: Path, color: tuple[int, int, int, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.new("RGBA", (4, 4), color)
    try:
        image.save(path, format="PNG")
    finally:
        image.close()


class AuthoredPbrPackTests(unittest.TestCase):
    def test_layer_precedence_and_path_mapping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            low = root / "low"
            high = root / "high"
            relative = Path("textures/architecture/metal/plate_s.dds")
            write_image(low / relative, (10, 20, 0, 255))
            write_image(high / relative, (30, 40, 0, 255))
            write_image(
                high / "textures/architecture/metal/plate_m.dds",
                (128, 128, 128, 255),
            )

            materials = MODULE.scan_layers(
                [MODULE.Layer("low", low), MODULE.Layer("high", high)]
            )

            self.assertEqual(len(materials), 1)
            material = materials[0]
            self.assertEqual(
                material.base_path,
                "textures\\architecture\\metal\\plate_d.dds",
            )
            self.assertEqual(material.specular.layer, "high")
            self.assertEqual(material.environment_mask.layer, "high")
            self.assertTrue(material.metallic_mask_enabled)
            self.assertTrue(
                material.rmaos_path.startswith(MODULE.RMAOS_NAMESPACE + "\\")
            )

    def test_rmaos_channel_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            specular = root / "textures/weapons/testblade_s.dds"
            mask = root / "textures/weapons/testblade_m.dds"
            write_image(specular, (255, 255, 9, 255))
            write_image(mask, (128, 40, 20, 255))
            material = MODULE.MaterialInput(
                base_path="textures\\weapons\\testblade_d.dds",
                rmaos_path=(
                    MODULE.RMAOS_NAMESPACE
                    + "\\weapons\\testblade_rmaos.dds"
                ),
                specular=MODULE.SourceFile("test", specular),
                environment_mask=MODULE.SourceFile("test", mask),
                metallic_mask_enabled=True,
            )

            image = MODULE.compose_rmaos(material)
            try:
                self.assertEqual(image.getpixel((0, 0)), (10, 128, 255, 20))
            finally:
                image.close()

    def test_nonmetal_masks_do_not_create_metalness(self) -> None:
        self.assertFalse(
            MODULE.is_high_confidence_metal(
                "textures\\landscape\\ground\\medrocks_d.dds", True
            )
        )
        self.assertFalse(
            MODULE.is_high_confidence_metal(
                "textures\\architecture\\buildings\\windows01_d.dds", True
            )
        )
        self.assertTrue(
            MODULE.is_high_confidence_metal(
                "textures\\actors\\handy\\handytorso_d.dds", True
            )
        )
        self.assertFalse(
            MODULE.is_high_confidence_metal(
                "textures\\actors\\character\\eyes\\eyeenvironmentmask_d.dds",
                True,
            )
        )
        self.assertTrue(
            MODULE.is_high_confidence_metal(
                "textures\\architecture\\powerplant\\metalpanel_d.dds", True
            )
        )

    def test_unsafe_texture_paths_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            MODULE.canonical_texture_path("../textures/test_s.dds")


if __name__ == "__main__":
    unittest.main()
