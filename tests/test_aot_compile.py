#!/usr/bin/env python3
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "aot_compile.py"

spec = importlib.util.spec_from_file_location("aot_compile", TOOL)
aot_compile = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = aot_compile
spec.loader.exec_module(aot_compile)


class AotCompileTests(unittest.TestCase):
    def compile_doc(self, doc):
        td = tempfile.TemporaryDirectory()
        root = Path(td.name)
        inp = root / "in.json"
        out_c = root / "out.c"
        out_h = root / "out.h"
        manifest = root / "manifest.json"
        text = json.dumps(doc, separators=(",", ":"), sort_keys=True)
        inp.write_text(text, encoding="utf-8")
        aot_compile.compile_input(inp, out_c, out_h, manifest)
        return td, text.encode("utf-8"), out_c.read_text(encoding="utf-8"), out_h.read_text(encoding="utf-8"), json.loads(manifest.read_text(encoding="utf-8"))

    def assert_cli_fails(self, doc, contains):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            inp = root / "in.json"
            inp.write_text(json.dumps(doc), encoding="utf-8")
            proc = subprocess.run(
                [sys.executable, str(TOOL), "--input", str(inp), "--output-c", str(root / "x.c"), "--output-h", str(root / "x.h"), "--manifest", str(root / "x.json")],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(proc.returncode, 2, proc.stderr)
            self.assertIn(contains, proc.stderr)

    def test_generates_deterministic_reachable_integer_path(self):
        doc = {"regions": [{"name": "integer_path", "base": 4096, "code": "b805000000bb0700000001d87505b80100000090", "entries": [0]}]}
        td, input_bytes, c, h, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn('#include "out.h"', c)
        self.assertIn('#include "xlle/aot_runtime.h"', h)
        self.assertIn("extern const xlle_aot_region xlle_aot_integer_path;", h)
        self.assertIn("const uint64_t start = state->retired;", c)
        self.assertIn("xlle_aot_check(region, state, bus, budget, start)", c)
        self.assertIn("case 0x00001000u", c)
        self.assertIn("case 0x0000100au", c)
        self.assertIn("state->gpr[0u] = xlle_aot_add", c)
        self.assertIn("if (guard != XLLE_AOT_CONTINUE) return guard;", c)
        self.assertEqual(manifest["version"], 1)
        self.assertEqual(manifest["input_sha256"], hashlib.sha256(input_bytes).hexdigest())
        decoded = manifest["regions"][0]["decoded"]
        self.assertEqual([d["offset"] for d in decoded], [0, 5, 10, 12, 14, 19])
        self.assertTrue(all(d["status"] == "supported" for d in decoded))

    def test_entries_default_and_explicit_resume(self):
        doc = {"regions": [{"name": "resume", "base": 0x2000, "code": "eb03909090", "entries": [0, 2]}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn("case 0x00002000u", c)
        self.assertIn("case 0x00002002u", c)
        self.assertIn("case 0x00002004u", c)
        self.assertEqual(manifest["regions"][0]["entries"], [0, 2])

    def test_external_branch_is_not_decoded_as_following_instruction(self):
        doc = {"regions": [{"name": "jump_out", "base": 0x3000, "code": "eb0590", "entries": [0]}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn("case 0x00003000u", c)
        self.assertNotIn("case 0x00003002u", c)
        edges = manifest["regions"][0]["decoded"][0]["edges"]
        self.assertEqual(edges, [{"internal": False, "kind": "jmp", "target": 7}])

    def test_conditional_branch_decodes_target_and_fallthrough(self):
        # cmp eax,eax ; je +5 ; mov eax,bad ; inc eax
        doc = {"regions": [{"name": "branch", "base": 0x1000, "code": "39c07405b8efbeadde40"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn("(state->eflags & 0x40u) ? target", c)
        self.assertIn("case 0x00001004u", c)
        self.assertIn("case 0x00001009u", c)
        decoded_offsets = [d["offset"] for d in manifest["regions"][0]["decoded"]]
        self.assertEqual(decoded_offsets, [0, 2, 4, 9])

    def test_branches_emit_machine_code_limit_guards(self):
        doc = {"independent_regions": True, "regions": [
            {"name": "taken_conditional", "base": 0xbffffff0, "code": "750e" + "90" * 14},
            {"name": "unconditional", "base": 0xbffffff0, "code": "eb0e" + "90" * 14},
        ]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn("if (!(state->eflags & 0x40u) && !xlle_aot_branch_allowed(bus, target)) return XLLE_AOT_CONTEXT;", c)
        self.assertIn("if (!xlle_aot_branch_allowed(bus, 0xc0000000u)) return XLLE_AOT_CONTEXT;", c)
        self.assertEqual(manifest["abi_version"], aot_compile.ABI_VERSION)

    def test_unsupported_terminal_does_not_guess_fallthrough(self):
        doc = {"regions": [{"name": "bad", "base": 0x1000, "code": "0f90"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        decoded = manifest["regions"][0]["decoded"]
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0]["status"], "unsupported")
        self.assertEqual(decoded[0]["offset"], 0)
        self.assertIsNone(decoded[0]["length"])
        self.assertEqual(decoded[0]["examined_length"], 1)
        self.assertNotIn("case 0x00001001u", c)
        self.assertIn("return XLLE_AOT_UNSUPPORTED;", c)

    def test_branch_target_into_immediate_is_rejected(self):
        # je +1 lands at offset 3, inside the following 5-byte mov immediate; the fallthrough decodes that mov.
        self.assert_cli_fails({"regions": [{"name": "overlap", "base": 0, "code": "7401b811223344"}]}, "overlaps instruction")

    def test_truncated_supported_instruction_rejected(self):
        self.assert_cli_fails({"regions": [{"name": "trunc", "base": 0, "code": "b81122"}]}, "truncated mov r32,imm32")
        self.assert_cli_fails({"regions": [{"name": "trunc2", "base": 0, "code": "e901"}]}, "truncated jmp rel32")

    def test_memory_arithmetic_form_is_terminal_unsupported(self):
        doc = {"regions": [{"name": "memform", "base": 0, "code": "010090"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        decoded = manifest["regions"][0]["decoded"]
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0]["mnemonic"], "unsupported_0x01_modrm_0x00")
        self.assertEqual(decoded[0]["status"], "unsupported")
        self.assertNotIn("case 0x00000002u", c)

    def test_modrm_sib_memory_mov_forms_emit_exact_effective_addresses(self):
        cases = [
            ("base_index_scale", "8944b3208b54b320",
             [
                 {"kind": "reg32", "value": 0},
                 {"kind": "mem32", "value": 0x20, "base": 3, "index": 6, "scale": 4},
             ],
             "(uint32_t)(0x00000020u + state->gpr[3u] + (state->gpr[6u] * 4u))"),
            ("abs_sib_index", "890c85002100008b148500210000",
             [
                 {"kind": "reg32", "value": 1},
                 {"kind": "mem32", "value": 0x2100, "base": -1, "index": 0, "scale": 4},
             ],
             "(uint32_t)(0x00002100u + (state->gpr[0u] * 4u))"),
            ("negative_disp8", "8945fc8b5dfc",
             [
                 {"kind": "reg32", "value": 0},
                 {"kind": "mem32", "value": 0xfffffffc, "base": 5, "index": -1, "scale": 1},
             ],
             "(uint32_t)(0xfffffffcu + state->gpr[5u])"),
            ("esp_disp32", "898c24300000008bbc2430000000",
             [
                 {"kind": "reg32", "value": 1},
                 {"kind": "mem32", "value": 0x30, "base": 4, "index": -1, "scale": 1},
             ],
             "(uint32_t)(0x00000030u + state->gpr[4u])"),
            ("wrap_disp32", "899810f0ffff8b8810f0ffff",
             [
                 {"kind": "reg32", "value": 3},
                 {"kind": "mem32", "value": 0xfffff010, "base": 0, "index": -1, "scale": 1},
             ],
             "(uint32_t)(0xfffff010u + state->gpr[0u])"),
        ]
        for name, code, first_operands, expression in cases:
            with self.subTest(name=name):
                td, _, c, _, manifest = self.compile_doc({"regions": [{"name": name, "base": 0x1000, "code": code}]})
                self.addCleanup(td.cleanup)
                decoded = manifest["regions"][0]["decoded"]
                self.assertEqual(decoded[0]["status"], "supported")
                self.assertEqual(decoded[0]["mnemonic"], "mov_r32_to_mem32")
                self.assertEqual(decoded[0]["operands"], first_operands)
                self.assertEqual(decoded[1]["status"], "supported")
                self.assertEqual(decoded[1]["mnemonic"], "mov_mem32_to_r32")
                self.assertIn(expression, c)

    def test_opcode83_sub_sign_extends_immediate(self):
        doc = {"regions": [{"name": "sub83", "base": 0x1000, "code": "83e80083eb7f83e88083efff"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        decoded = manifest["regions"][0]["decoded"]
        self.assertEqual([d["mnemonic"] for d in decoded], ["sub_r32_imm32"] * 4)
        self.assertEqual([d["operands"][0]["value"] for d in decoded], [0, 3, 0, 7])
        self.assertEqual([d["operands"][1]["value"] for d in decoded],
                         [0x00000000, 0x0000007f, 0xffffff80, 0xffffffff])
        self.assertIn("state->gpr[0u] = xlle_aot_sub(state, state->gpr[0u], 0xffffff80u);", c)
        self.assertIn("state->gpr[7u] = xlle_aot_sub(state, state->gpr[7u], 0xffffffffu);", c)

    def test_malformed_region_inputs_rejected(self):
        bad_docs = [
            ({"regions": []}, "regions"),
            ({"regions": [{"name": "1bad", "base": 0, "code": "90"}]}, "malformed region name"),
            ({"regions": [{"name": "bad", "base": -1, "code": "90"}]}, "base must be uint32"),
            ({"regions": [{"name": "bad", "base": 0xFFFFFFFF, "code": "90"}]}, "base+code_size"),
            ({"regions": [{"name": "a", "base": 0x1000, "code": "90"}, {"name": "b", "base": 0x1000, "code": "90"}]}, "overlaps region"),
            ({"regions": [{"name": "bad", "base": 0, "code": "9"}]}, "even-length"),
            ({"regions": [{"name": "bad", "base": 0, "code": "zz"}]}, "valid hex"),
            ({"regions": [{"name": "bad", "base": 0, "code": ""}]}, "code is empty"),
            ({"regions": [{"name": "bad", "base": 0, "code": "90", "entries": [1]}]}, "entry must be an offset"),
            ({"regions": [{"name": "dup", "base": 0, "code": "90"}, {"name": "dup", "base": 1, "code": "90"}]}, "duplicate region name"),
        ]
        for doc, needle in bad_docs:
            with self.subTest(needle=needle):
                self.assert_cli_fails(doc, needle)

    def test_emits_byte_register_and_memory_helpers(self):
        doc = {"regions": [{"name": "helpers", "base": 0x4000, "code": "b411b02288e38ae1a300200000a100200000"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertIn("xlle_aot_set8(state, 4u, 0x11u);", c)
        self.assertIn("xlle_aot_set8(state, 3u, xlle_aot_get8(state, 4u));", c)
        self.assertIn("xlle_aot_write32(bus, 0x00002000u, state->gpr[0])", c)
        self.assertIn("xlle_aot_read32(bus, 0x00002000u, &value)", c)
        self.assertEqual([d["mnemonic"] for d in manifest["regions"][0]["decoded"]], [
            "mov_r8_imm8", "mov_r8_imm8", "mov_r8_rm8_to_rm8", "mov_rm8_to_r8", "mov_moffs32_eax", "mov_eax_moffs32"
        ])

    def test_generation_is_repeatable(self):
        doc = {"regions": [{"name": "repeat", "base": 0x1000, "code": "b80100000040"}]}
        first = self.compile_doc(doc)
        second = self.compile_doc(doc)
        self.addCleanup(first[0].cleanup)
        self.addCleanup(second[0].cleanup)
        self.assertEqual(first[2], second[2])
        self.assertEqual(first[3], second[3])
        self.assertEqual(first[4], second[4])

    def test_independent_artifacts_require_explicit_selection(self):
        doc = {"independent_regions": True, "regions": [
            {"name": "variant_a", "base": 4096, "code": "90"},
            {"name": "variant_b", "base": 4096, "code": "40"}]}
        td, _, c, _, manifest = self.compile_doc(doc)
        self.addCleanup(td.cleanup)
        self.assertTrue(manifest["independent_regions"])
        self.assertIn("region != &xlle_aot_variant_a", c)
        self.assertIn("region != &xlle_aot_variant_b", c)
        self.assertNotEqual(manifest["regions"][0]["code_sha256"],
                            manifest["regions"][1]["code_sha256"])

    def test_schema_and_generated_symbol_collisions(self):
        for region in [
            {"name": "boolean_base", "base": True, "code": "90"},
            {"name": "boolean_entry", "base": 0, "code": "90", "entries": [False]},
            {"name": "null_entries", "base": 0, "code": "90", "entries": None},
            {"name": "large", "base": 0, "code": "90" * 65537}]:
            with self.subTest(name=region["name"]):
                with self.assertRaises(aot_compile.AotError):
                    aot_compile.parse_region(region)
        self.assert_cli_fails({"regions": [
            {"name": "a", "base": 0, "code": "90"},
            {"name": "a_run", "base": 1, "code": "90"}]}, "symbol collision")

    def test_cannot_overwrite_input(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            inp = root / "input.json"
            inp.write_text('{"regions": []}', encoding="utf-8")
            with self.assertRaisesRegex(aot_compile.AotError, "distinct"):
                aot_compile.compile_input(inp, inp, root / "a.h", root / "m.json")
            self.assertEqual(inp.read_text(), '{"regions": []}')


if __name__ == "__main__":
    unittest.main()
