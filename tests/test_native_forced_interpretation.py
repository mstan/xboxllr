"""Forced-interpretation stubs replace exactly the selected generated bodies."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_forced_interpretation as forced  # noqa: E402

UNIT = """#include "recomp_funcs.h"
void sub_00011000(void)
{
    /* braces in comments { and strings must not confuse the scanner } */
    const char *s = "}{";
    if (eax) { eax = 1; } else { eax = 2; }
    esp += 4; return;
}

void sub_00012000(void)
{
    sub_00011000();
    esp += 4; return;
}
"""


class ForcedInterpretation(unittest.TestCase):
    def test_replaces_only_selected_body(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "recomp_0001.c").write_text(UNIT, encoding="utf-8")
            replaced = forced.apply(d, [0x11000])
            text = (Path(d) / "recomp_0001.c").read_text(encoding="utf-8")
        self.assertEqual(replaced, [dict(va="0x00011000", unit="recomp_0001.c")])
        self.assertIn('RECOMP_UNIMPL_FUNCTION("qualification: forced interpretation", 0x00011000u, 5u);', text)
        self.assertNotIn('"}{"', text)
        self.assertIn("void sub_00012000(void)\n{\n    sub_00011000();", text)

    def test_unknown_function_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "recomp_0001.c").write_text(UNIT, encoding="utf-8")
            with self.assertRaises(ValueError):
                forced.apply(d, [0x13000])

    def test_load_accepts_hex_strings(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "forced.json"
            p.write_text(json.dumps({"functions": ["0x00012000", "0x00011000"]}), encoding="utf-8")
            self.assertEqual(forced.load(p), [0x11000, 0x12000])
            p.write_text(json.dumps({"functions": ["0x00000010"]}), encoding="utf-8")
            with self.assertRaises(ValueError):
                forced.load(p)


if __name__ == "__main__":
    unittest.main()
