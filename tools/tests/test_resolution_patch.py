"""Keep projection and target geometry consistent without modifying unrelated constants."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "probe_uncharted", Path(__file__).resolve().parents[1] / "probe_uncharted.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def fixture(base):
    image = bytearray(0x5000)
    stores = [(8, 1920), (12, 1080), (24, 1920), (28, 1080),
              (32, 0x3fe38e39), (36, 1920), (40, 1080), (44, 0x3fe38e39),
              (56, 1920), (60, 1080), (64, 240), (68, 270)]
    for index, (disp, value) in enumerate(stores):
        struct.pack_into("<BBBI", image, 0x100 + 7 * index, 0xc7, base, disp, value)
    for index, (ident, parent, size, align) in enumerate([
            (2, 2, 1536 << 20, 1 << 20), (3, 2, 2988 << 20, 1 << 20),
            (0x85, 6, 497 << 20, 0x80), (0x84, 6, 140 << 20, 0x80)]):
        struct.pack_into("<IIQQQ", image, 0x3000 + 32 * index, ident, parent, size, align, 0)
    image[0x4000:0x400e] = bytes.fromhex("488d75e048ba0000002001000000")
    image[0x4200:0x4207] = bytes.fromhex("c74720398ee33f")  # unrelated 16:9 constant
    return image, stores


class ResolutionPatchTests(unittest.TestCase):
    def test_projection_and_targets_follow_each_shape(self):
        for base in (0x40, 0x47):  # game versions 1.02 and 1.00
            for width, height in ((1920, 1080), (1920, 1200), (1280, 800), (2560, 1080)):
                with self.subTest(base=base, size=(width, height)):
                    image, stores = fixture(base)
                    original_tail = image[0x4200:]
                    with patch.dict(os.environ, {"UNCHARTED_RES_KEEP": ""}), contextlib.redirect_stdout(io.StringIO()):
                        probe.resolution_patch(image, width, height)
                    for index, (disp, _) in enumerate(stores):
                        actual = struct.unpack_from("<I", image, 0x103 + 7 * index)[0]
                        if disp in (32, 44):
                            actual = struct.unpack("<f", struct.pack("<I", actual))[0]
                            self.assertAlmostEqual(actual, width / height, places=6)
                        elif disp in (8, 24, 36, 56):
                            self.assertEqual(actual, width)
                        elif disp in (12, 28, 40, 60):
                            self.assertEqual(actual, height)
                    self.assertEqual(image[0x4200:], original_tail)

    def test_explicit_diagnostic_keep_is_preserved(self):
        image, _ = fixture(0x47)
        with patch.dict(os.environ, {"UNCHARTED_RES_KEEP": "20"}), contextlib.redirect_stdout(io.StringIO()):
            probe.resolution_patch(image, 1920, 1200)
        self.assertEqual(struct.unpack_from("<I", image, 0x103 + 4 * 7)[0], 0x3fe38e39)
        self.assertAlmostEqual(struct.unpack_from("<f", image, 0x103 + 7 * 7)[0], 1.6, places=6)


if __name__ == "__main__":
    unittest.main()
