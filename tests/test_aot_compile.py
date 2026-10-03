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
    def test_x87_fixed_operations_and_environment_metadata(self):
        for opcode, form, mnemonic in [(0xd9,0,"LOAD32"),(0xdd,0,"LOAD64"),
                (0xd8,1,"MUL32"),(0xdc,1,"MUL64"),(0xd9,2,"STORE32"),
                (0xdd,2,"STORE64"),(0xd9,3,"STORE_POP32"),(0xdd,3,"STORE_POP64")]:
            for modrm, suffix, segment in [(0x00,b"",0),(0x45,b"\xfc",1),
                    (0x04,b"\x2b",0),(0x04,b"\x25\x00\x20\x00\x00",0)]:
                raw=modrm | form*8
                inst=aot_compile.decode_one(bytes([opcode,raw])+suffix,0)
                self.assertEqual(inst.mnemonic,"x87_mem_"+mnemonic)
                self.assertEqual(inst.operands[1].value,((opcode&7)<<8)|raw)
                self.assertEqual(inst.operands[2].value,segment)
        for opcode,base,name in [(0xd9,0xc0,"LOAD_REG"),(0xd9,0xc8,"EXCHANGE"),
                                 (0xde,0xc0,"ADD_POP"),(0xdd,0xd8,"STORE_POP_REG"),
                                 (0xd8,0xc8,"MUL_REG"),(0xde,0xe8,"SUB_POP"),(0xdd,0xc0,"FREE_REG")]:
            for index in range(8):
                inst=aot_compile.decode_one(bytes([opcode,base+index]),0)
                self.assertEqual(inst.mnemonic,"x87_reg_"+name)
                self.assertEqual(inst.operands[0].value,index)
        for raw in ("66d906","67d906","64d906","f0d906","f3d906","d9d1","de08"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,"unsupported")
        for raw in ("d9","d904","d945","d985010203"):
            with self.assertRaises(aot_compile.AotError): aot_compile.decode_one(bytes.fromhex(raw),0)

    def test_test32_encodings(self):
        for source in range(8):
            for dest in range(8):
                inst=aot_compile.decode_one(bytes([0x85,0xc0+source*8+dest]),0)
                self.assertEqual(inst.mnemonic,"test_r32_rm32")
                self.assertEqual([o.value for o in inst.operands],[source,dest])
        for dest in range(8):
            inst=aot_compile.decode_one(bytes([0xf7,0xc0+dest,0x78,0x56,0x34,0x12]),0)
            self.assertEqual(inst.mnemonic,"test_r32_imm32")
            self.assertEqual([o.value for o in inst.operands],[dest,0x12345678])
        self.assertEqual(aot_compile.decode_one(bytes.fromhex("a978563412"),0).mnemonic,"test_eax_imm32")
        for raw in ("6685c0","85","f7","f7c012","a91234"):
            if raw.startswith("66"):
                self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,"unsupported")
            else:
                with self.assertRaises(aot_compile.AotError): aot_compile.decode_one(bytes.fromhex(raw),0)
        for raw in ("8500","f700","f7c8"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,"unsupported")

    def test_rotate32_encodings(self):
        for right in (0, 1):
            for destination in range(8):
                modrm = 0xc0 + 8 * right + destination
                for opcode in (0xc1, 0xd1, 0xd3):
                    for count in (range(256) if opcode == 0xc1 else (1,)):
                        code = bytes([opcode, modrm] + ([count] if opcode == 0xc1 else []))
                        inst = aot_compile.decode_one(code, 0)
                        self.assertEqual(inst.mnemonic, "ror_r32" if right else "rol_r32")
                        self.assertEqual(inst.length, len(code))
                        self.assertEqual(inst.operands[0], aot_compile.Operand("reg32", destination))
                        self.assertEqual(inst.operands[1], aot_compile.Operand(
                            "reg8" if opcode == 0xd3 else "imm8", 1 if opcode != 0xc1 else count))
        for raw in ("c10001", "d108", "d308", "c0c001", "d0c0", "d2c0", "c1d001",
                    "d1d8", "d3f0", "66d1c0", "67d1c0", "f0d1c0", "f3d3c0"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw), 0).status, "unsupported")
        for raw in ("c1", "c1c0", "d1", "d3"):
            with self.assertRaises(aot_compile.AotError):
                aot_compile.decode_one(bytes.fromhex(raw), 0)

    def test_or32_and_bswap_encodings(self):
        for destination in range(8):
            inst = aot_compile.decode_one(bytes([0x0f, 0xc8 + destination]), 0)
            self.assertEqual(inst.mnemonic, "bswap_r32")
            self.assertEqual(inst.operands, [aot_compile.Operand("reg32", destination)])
            for source in range(8):
                direct = aot_compile.decode_one(bytes([0x09, 0xc0 + source * 8 + destination]), 0)
                reverse = aot_compile.decode_one(bytes([0x0b, 0xc0 + destination * 8 + source]), 0)
                self.assertEqual(direct, reverse)
                self.assertEqual(direct.mnemonic, "or_r32_to_rm32")
                self.assertEqual([o.value for o in direct.operands], [source, destination])
            for immediate in range(256):
                inst = aot_compile.decode_one(bytes([0x83, 0xc8 + destination, immediate]), 0)
                self.assertEqual(inst.mnemonic, "or_r32_imm32")
                self.assertEqual([o.value for o in inst.operands],
                                 [destination, immediate if immediate < 128 else immediate + 0xffffff00])
            inst = aot_compile.decode_one(bytes([0x81, 0xc8 + destination, 0x78, 0x56, 0x34, 0x12]), 0)
            self.assertEqual([o.value for o in inst.operands], [destination, 0x12345678])
        self.assertEqual(aot_compile.decode_one(bytes.fromhex("0d78563412"), 0).mnemonic, "or_eax_imm32")
        for raw in ("660fc8", "f00fc8", "6609d8", "0900", "0b00", "0800", "8108", "8308"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw), 0).status, "unsupported")
        for raw in ("09", "0b", "0d", "81c8", "83c8"):
            with self.assertRaises(aot_compile.AotError):
                aot_compile.decode_one(bytes.fromhex(raw), 0)

    def test_shift32_encodings(self):
        for group, name in ((4,"shl"),(5,"shr"),(7,"sar")):
            for reg in range(8):
                for opcode in (0xc1,0xd1,0xd3):
                    for count in (range(256) if opcode==0xc1 else (1,)):
                        raw=bytes([opcode,0xc0+group*8+reg]+([count] if opcode==0xc1 else []))
                        inst=aot_compile.decode_one(raw,0)
                        self.assertEqual(inst.mnemonic,name+"_r32")
                        self.assertEqual(inst.length,len(raw))
                        self.assertEqual(inst.operands,[aot_compile.Operand("reg32",reg),
                            aot_compile.Operand("reg8" if opcode==0xd3 else "imm8",count)])
        for raw in ("d120","c12801","d338","d0e0","66d1e0","f0d1e0","d1f0"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,"unsupported")

    def test_all_near_condition_encodings(self):
        for condition,name in enumerate(aot_compile.CC_NAMES):
            for near in (False,True):
                length=6 if near else 2
                for displacement in (-length,-1,0,1,127):
                    raw=(bytes([0x0f,0x80+condition])+displacement.to_bytes(4,"little",signed=True)
                         if near else bytes([0x70+condition,displacement&255]))
                    inst=aot_compile.decode_one(raw,0)
                    self.assertEqual(inst.mnemonic,name+("_rel32" if near else "_rel8"))
                    self.assertEqual(inst.length,length)
                    self.assertEqual(inst.edges[0]['target'],length+displacement)
                    self.assertEqual(inst.edges[1]['kind'],'fallthrough')
                with self.assertRaises(aot_compile.AotError):aot_compile.decode_one(raw[:-1],0)
        for raw in ('667001','660f8201000000','f07401','e301'):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,'unsupported')

    def test_movzx_and_immediate_memory_forms(self):
        for width,opcode in ((8,0xb6),(16,0xb7)):
            for dest in range(8):
                for source in range(8):
                    inst=aot_compile.decode_one(bytes([0x0f,opcode,0xc0+dest*8+source]),0)
                    self.assertEqual(inst.operands,[aot_compile.Operand('reg32',dest),aot_compile.Operand(f'reg{width}',source)])
            for suffix in ('06','44b320','45fc','842430000000','048500210000'):
                inst=aot_compile.decode_one(bytes([0x0f,opcode])+bytes.fromhex(suffix),0)
                self.assertEqual(inst.operands[1].kind,f'mem{width}')
            inst=aot_compile.decode_one(bytes([0x0f,opcode])+bytes.fromhex('44b3fc'),0)
            self.assertEqual(aot_compile.operand_manifest(inst.operands[1]),
                             {'kind':f'mem{width}','value':0xfffffffc,'base':3,'index':6,'scale':4})
        for reg in range(8):
            inst=aot_compile.decode_one(bytes([0xc7,0xc0+reg,0x78,0x56,0x34,0x12]),0)
            self.assertEqual(inst.mnemonic,'mov_r32_imm32')
            self.assertEqual([o.value for o in inst.operands],[reg,0x12345678])
        inst=aot_compile.decode_one(bytes.fromhex('c744b3fc78563412'),0)
        self.assertEqual(inst.mnemonic,'mov_imm32_to_mem32')
        self.assertEqual(inst.operands,[aot_compile.Operand('mem32',0xfffffffc,3,6,4),aot_compile.Operand('imm32',0x12345678)])
        for raw in ('c7','c700','c704','c745fc1234','0fb6','0fb744','0fb78501'):
            with self.assertRaises(aot_compile.AotError):aot_compile.decode_one(bytes.fromhex(raw),0)
        for raw in ('66c7001234','67c700','f0c700','c708','c7f8','660fb6c0','670fb700','f00fb6c0'):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(raw),0).status,'unsupported')

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

    def test_calls_recover_callee_and_return_continuation(self):
        region = aot_compile.parse_region({"name": "calls", "base": 4096,
                                          "code": "e803000000c30f0b40c3"})
        self.assertEqual(sorted(region["decoded"]), [0, 5, 8, 9])
        self.assertEqual(region["decoded"][5].edges, [])
        self.assertEqual(region["decoded"][9].edges, [])

    def test_stack_encodings_and_dynamic_target_discovery(self):
        for code, name in (("54", "push_r32"), ("5c", "pop_r32"), ("6aff", "push_imm32"),
                           ("c20800", "ret_near"), ("ffd4", "call_r32"), ("ffe0", "jmp_r32")):
            with self.subTest(code=code):
                inst = aot_compile.decode_one(bytes.fromhex(code), 0)
                self.assertEqual(inst.mnemonic, name)
                self.assertEqual(inst.length, len(bytes.fromhex(code)))
        self.assertEqual(aot_compile.decode_one(bytes.fromhex("6aff"), 0).operands[0].value, 0xffffffff)
        self.assert_cli_fails({"regions": [{"name": "badret", "base": 4096, "code": "c208"}]}, "truncated ret")
        self.assert_cli_fails({"regions": [{"name": "badcall", "base": 4096, "code": "e808"}]}, "truncated call")

    def test_arithmetic_register_encoding_directions_agree(self):
        for left, right in (("01d8", "03c3"), ("29d8", "2bc3"), ("31d8", "33c3"),
                            ("21d8", "23c3"), ("39d8", "3bc3")):
            with self.subTest(left=left, right=right):
                self.assertEqual(aot_compile.decode_one(bytes.fromhex(left), 0),
                                 aot_compile.decode_one(bytes.fromhex(right), 0))
        for opcode in (0x03, 0x2b, 0x33, 0x23, 0x3b):
            self.assertEqual(aot_compile.decode_one(bytes([opcode, 0]), 0).status, "unsupported")

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

    def test_lea_address_forms_and_invalid_register_encoding(self):
        for code, destination, address in (
                ("8d44b320", 0, (0x20, 3, 6, 4)),
                ("8d048500210000", 0, (0x2100, -1, 0, 4)),
                ("8d6dfc", 5, (0xfffffffc, 5, -1, 1)),
                ("8da42430000000", 4, (0x30, 4, -1, 1)),
                ("8d1c2500000080", 3, (0x80000000, -1, -1, 1))):
            with self.subTest(code=code):
                inst = aot_compile.decode_one(bytes.fromhex(code), 0)
                self.assertEqual(inst.mnemonic, "lea_r32")
                self.assertEqual(inst.length, len(bytes.fromhex(code)))
                self.assertEqual(inst.operands[0].value, destination)
                operand = inst.operands[1]
                self.assertEqual((operand.value, operand.base, operand.index, operand.scale), address)
        # Invalid and prefixed forms must remain explicit unsupported exits.
        for code in ("8dc0", "668d00", "678d00", "f08d00"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(code), 0).status, "unsupported")
        for code in ("8d", "8d04", "8d0425", "8d80ffff"):
            with self.subTest(code=code), self.assertRaises(aot_compile.AotError):
                aot_compile.decode_one(bytes.fromhex(code), 0)

    def test_mmx_transfer_forms_and_precise_unsupported_encodings(self):
        for code, name, regs in (("0f77", "mmx_emms", []),
                                 ("0f6fd5", "mmx_move", [2, 5]),
                                 ("0f7fea", "mmx_move", [2, 5]),
                                 ("0f6f44b320", "mmx_load64", [0]),
                                 ("0f7f4c24fc", "mmx_store64", [1]),
                                 ("0fe71d00200000", "mmx_store_nt64", [3])):
            with self.subTest(code=code):
                inst = aot_compile.decode_one(bytes.fromhex(code), 0)
                self.assertEqual((inst.mnemonic, inst.length), (name, len(bytes.fromhex(code))))
                self.assertEqual([op.value for op in inst.operands if op.kind == "mmx"], regs)
                if name in ("mmx_load64", "mmx_store64", "mmx_store_nt64"):
                    self.assertEqual(inst.operands[1].kind, "mem64")
        for code in ("0fe7c0", "660f6fc0", "f30f7fc0", "f00f6f00", "670f6f00"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(code), 0).status, "unsupported")
        for code in ("0f6f", "0f7f04", "0fe7051122"):
            with self.subTest(code=code), self.assertRaises(aot_compile.AotError):
                aot_compile.decode_one(bytes.fromhex(code), 0)

    def test_prefetch_localities_and_unsupported_forms(self):
        for locality in range(4):
            inst = aot_compile.decode_one(bytes([0x0f, 0x18, 6 | (locality << 3)]), 0)
            self.assertEqual(inst.mnemonic, "prefetch")
            self.assertEqual(inst.operands[0].value, locality)
        for code in ("0f18c0", "0f1820", "f00f1800", "670f1800"):
            self.assertEqual(aot_compile.decode_one(bytes.fromhex(code), 0).status, "unsupported")

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
