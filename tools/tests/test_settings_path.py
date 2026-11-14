"""Guest preparation and runtime must not read different profiles for the same launch."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "probe_settings", Path(__file__).resolve().parents[1] / "probe_uncharted.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class SettingsPathTests(unittest.TestCase):
    def test_explicit_profile_wins_over_installation_and_legacy_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            user = root / "user"
            user.mkdir()
            (root / "EngineConfig.ini").write_text("upscaler=fsr3\n")
            (user / "probe-u1.ini").write_text("upscaler=taa\n")
            isolated = root / "isolated.ini"
            isolated.write_text("upscaler=off\n")
            with patch.object(probe, "ROOT", root), patch.dict(os.environ, {
                    "UNCHARTED_CONFIG_FILE": str(isolated),
                    "GOW3_CONFIG": str(root / "other.ini")}, clear=True):
                for game in ("u1", "u2", "u3"):
                    actual = probe.settings_path(user, game)
                    self.assertEqual(actual, isolated)
                    self.assertEqual(probe.ini_value(actual, "upscaler", "missing"), "off")
                self.assertEqual((root / "EngineConfig.ini").read_text(), "upscaler=fsr3\n")

    def test_runtime_config_is_honoured_without_launcher_alias(self):
        with patch.dict(os.environ, {"GOW3_CONFIG": "custom.ini"}, clear=True):
            self.assertEqual(probe.settings_path(Path("player"), "u2"), Path("custom.ini"))

    def test_installation_per_game_and_legacy_fallback_order(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {}, clear=True):
            root = Path(temp)
            user = root / "user"
            user.mkdir()
            with patch.object(probe, "ROOT", root):
                self.assertEqual(probe.settings_path(user, "u3"), user / "probe-u3.ini")
                (user / "probe.ini").write_text("upscaler=off\n")
                self.assertEqual(probe.settings_path(user, "u3"), user / "probe.ini")
                (user / "probe-u3.ini").write_text("upscaler=taa\n")
                self.assertEqual(probe.settings_path(user, "u3"), user / "probe-u3.ini")
                (root / "EngineConfig.ini").write_text("upscaler=fsr3\n")
                self.assertEqual(probe.settings_path(user, "u3"), root / "EngineConfig.ini")


if __name__ == "__main__":
    unittest.main()
