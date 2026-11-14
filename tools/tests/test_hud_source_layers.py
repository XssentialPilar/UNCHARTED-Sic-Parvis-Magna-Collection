"""Removing an unbound control must leave the journal/map art and source ZIP intact."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "hud_baker", Path(__file__).resolve().parents[1] / "bake_hud_prompt_bank.py")
baker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baker)


class HudSourceLayerTests(unittest.TestCase):
    def test_only_explicit_control_layer_is_removed(self):
        source = b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 665 430"><defs><image id="book" href="data:image/png;base64,AAAA"/></defs><use href="#book" filter="url(#glow)"/><use href="#book"/><svg aria-label="TAB" viewBox="0 0 192 128"><path d="M0 0L1 1"/></svg></svg>'
        original = ET.fromstring(source)
        result = ET.fromstring(baker.without_binding(source))
        self.assertEqual(result.attrib, original.attrib)
        self.assertEqual([child.tag.rsplit("}", 1)[-1] for child in result],
                         ["defs", "use", "use"])
        for before, after in zip(list(original)[:3], result):
            self.assertEqual(ET.tostring(before), ET.tostring(after))

    def test_ambiguous_layers_are_not_guessed(self):
        for source in (b'<svg xmlns="http://www.w3.org/2000/svg"/>',
                       b'<svg xmlns="http://www.w3.org/2000/svg"><svg/><svg/></svg>'):
            with self.assertRaises(ValueError):
                baker.without_binding(source)


if __name__ == "__main__":
    unittest.main()
