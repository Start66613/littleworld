import importlib.util
from pathlib import Path
import unittest
from PIL import Image

PATH = Path(__file__).resolve().parents[1] / "tools/analyze_desktop_pet.py"
spec = importlib.util.spec_from_file_location("desktop_pet_export", PATH)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)
SOURCE = Path("D:/Espressif/LittleWorld/sources/DesktopPet")


class DesktopPetExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clips = {c["name"]: c for c in export.parse_frames(SOURCE)}

    def test_runtime_resource_identity_and_animation_count(self):
        self.assertEqual(len(self.clips), 19)
        self.assertEqual(len(self.clips["idle"]["frames"]), 8)
        self.assertIn("dog_interaction", self.clips)

    def test_duration_weights_are_not_discarded(self):
        self.assertEqual(self.clips["idle"]["frames"][0]["duration_ms"], 125)
        self.assertEqual(self.clips["walk_right"]["frames"][0]["duration_ms"], 100)
        self.assertEqual(self.clips["sleep"]["frames"][0]["duration_ms"], 400)

    def test_fractional_duration_rounding_does_not_accumulate(self):
        clip = self.clips["music"]
        expected = round(sum(f["duration_weight"] for f in clip["frames"]) * 1000 / clip["speed"])
        self.assertEqual(sum(f["duration_ms"] for f in clip["frames"]), expected)
        self.assertIn(134, [f["duration_ms"] for f in clip["frames"]])

    def test_atlas_rectangles_extract_the_actual_frame(self):
        frame = self.clips["walk_right"]["frames"][1]
        image = export.original(SOURCE, frame)
        self.assertEqual(image.size, (frame["region"][2], frame["region"][3]))
        self.assertIsNotNone(image.getchannel("A").getbbox())

    def test_native_idle_is_not_scaled(self):
        image = export.original(SOURCE, self.clips["idle"]["frames"][0])
        self.assertEqual(image.size, (256, 256))
        self.assertEqual(image.getchannel("A").getbbox(), (78, 28, 176, 226))

    def test_sleep_floor_comes_from_actual_art(self):
        for frame in self.clips["sleep_idle"]["frames"]:
            alpha = export.original(SOURCE, frame).getchannel("A")
            solid = alpha.point(lambda a: 255 if a >= 128 else 0)
            self.assertEqual(solid.getbbox()[3], 216)

    def test_rgb565a8_little_endian_and_separate_alpha(self):
        image = Image.new("RGBA", (2, 1))
        image.putpixel((0, 0), (255, 0, 0, 255))
        image.putpixel((1, 0), (255, 255, 255, 0))
        self.assertEqual(export.rgb565a8(image), bytes([0, 248, 0, 0, 255, 0]))

    def test_source_path_escape_is_rejected(self):
        with self.assertRaises(ValueError):
            export.original(SOURCE, {"source": "../outside.png", "region": None})


if __name__ == "__main__":
    unittest.main()
