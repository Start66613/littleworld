import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image, ImageDraw

MODULE = Path(__file__).resolve().parents[1] / "tools/audit_assets.py"
spec = importlib.util.spec_from_file_location("audit_assets", MODULE)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AssetAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        image = Image.new("RGBA", (256, 256))
        ImageDraw.Draw(image).ellipse((48, 40, 208, 236), fill=(220, 170, 210, 255))
        file = self.root / "frame_000.png"
        image.save(file)
        self.asset = {"id": "pet.idle", "kind": "sprite", "status": "ready", "width": 256, "height": 256,
                      "alpha": "straight", "anchor": [128, 236], "fps": 8, "loop": True,
                      "files": [{"path": file.name, "sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "duration_ms": 125}],
                      "license": "test fixture only", "provenance": "generated locally with Pillow; not production artwork"}
        self.manifest = {"schema": "littleworld.assets/2", "contract_version": "1.0.0", "asset_version": "test", "producer": "unit-test", "assets": [self.asset]}

    def tearDown(self):
        self.temp.cleanup()

    def result(self):
        return audit.inspect(self.manifest, self.root)

    def test_technical_pass_never_grants_visual_acceptance(self):
        result = self.result()
        self.assertEqual(result["technical_status"], "pass")
        self.assertFalse(result["accepted_for_integration"])
        self.assertEqual(result["all_frames_raw_storage_bytes"], 196608)

    def test_empty_old_manifest_is_waiting(self):
        self.assertEqual(audit.inspect({"schema": "littleworld.assets/1", "assets": []}, self.root)["technical_status"], "waiting")

    def test_hash_mismatch(self):
        self.asset["files"][0]["sha256"] = "0" * 64
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_traversal_and_absolute_paths(self):
        for name in ("../frame_000.png", "C:\\secrets.png", "/outside.png", "private/photo.png"):
            self.asset["files"][0]["path"] = name
            self.assertEqual(self.result()["technical_status"], "fail")

    def test_dimensions_and_anchor(self):
        self.asset["anchor"] = [128, 240]
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_fake_alpha(self):
        image = Image.new("RGB", (256, 256), "white")
        file = self.root / "frame_000.png"
        image.save(file)
        self.asset["files"][0]["sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_duplicate_id(self):
        self.manifest["assets"].append(self.asset.copy())
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_missing_frame_duration(self):
        del self.asset["files"][0]["duration_ms"]
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_sheet_mismatch(self):
        file = self.root / "sheet.png"
        Image.new("RGBA", (256, 256)).save(file)
        self.asset["sheet"] = {"path": file.name, "sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "columns": 1, "rows": 1, "frame_count": 1}
        self.assertEqual(self.result()["technical_status"], "fail")

    def test_wrong_schema(self):
        self.manifest["schema"] = "littleworld.assets/1"
        self.assertEqual(self.result()["technical_status"], "fail")


if __name__ == "__main__":
    unittest.main()
