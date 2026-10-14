#!/usr/bin/env python3
"""Bounded unchanged-original default-context GP frame; no full shell claim."""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path

EXPECTED = {
    "control-ram-0.aot-private.bin": "9d5b402d1ca6b67445743bd86bdd06fb9e63c4ffd936c98d011c76f61a0669a7",
    "control-ownership-0.aot-private.bin": "534f52f79cc617b0b153a2f6095b177ffa85e010602ff25e43371769ff15eb60",
    "control-registers-0-frames.aot-private.bin": "1ab6570a27d055fe19d3e86c11440e2cb29da900f66c10cd7a03d38fc19c11b1",
}
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--executable", type=Path, required=True)
    ap.add_argument("--fixture", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    for name, expected in EXPECTED.items():
        if sha(a.fixture/name) != expected: raise SystemExit("Frozen input identity mismatch: " + name)
    if a.output.exists(): raise SystemExit("Output must be a new directory")
    a.output.mkdir(parents=True)
    capture = a.output/"frame.aot-private.bin"
    command = [str(a.executable.resolve()), str((a.fixture/"control-ram-0.aot-private.bin").resolve()),
        str((a.fixture/"control-registers-0-frames.aot-private.bin").resolve()),
        str((a.fixture/"control-ownership-0.aot-private.bin").resolve()),
        "196608", "278528", "245760", "frames", str(capture.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (a.output/"run.log").write_text(result.stdout+result.stderr)
    if result.returncode: raise SystemExit("Original continuation stopped: " + result.stderr)
    observed = json.loads(result.stdout)
    if observed["status"] != "original_default_gp_frame_continued" or observed["full_shell_qualified"] or observed["ep_started"]:
        raise SystemExit("Unexpected continuation scope")
    data = Path(str(capture)+".mix-input.aot-private.bin").read_bytes()
    if len(data) != 4096 or len(capture.read_bytes()) != 3328: raise SystemExit("Capture length mismatch")
    # Independent byte oracle: published real VP MIX ordering, no DSP-derived expected output.
    expected = b"".join(data[c*128:(c+1)*128] for c in (0,2,1,4,5,3))+data[11*128:31*128]
    if capture.read_bytes() != expected: raise SystemExit("Original output byte mismatch")
    report = {"observed": observed, "independent_byte_oracle": True,
        "inputs": {n: sha(a.fixture/n) for n in EXPECTED}, "executable_sha256": sha(a.executable),
        "capture_sha256": sha(capture), "mix_input_sha256": hashlib.sha256(data).hexdigest(),
        "limits": ["One default zero-context GP frame only", "Owned diagnostic PCM and voice configuration are authored inputs",
          "No original wave constructor, nonzero program directory publication, EP execution or full shell qualification"],
        "next_gate": "Actual original producer of nonzero scratch control packet/directory, before dynamic program and parameter reload admission"}
    (a.output/"report.aot-private.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))
if __name__ == "__main__": main()
