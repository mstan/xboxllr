#!/usr/bin/env python3
"""Command-line entry point for the early XboxLLR build ecosystem."""
from pathlib import Path
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMANDS = {'title': 'native_title.py', 'prepare': 'native_pilot.py',
            'dependencies': 'native_release_sources.py'}
HELP = """XboxLLR beta - original Xbox low-level recompiler ecosystem

Usage: xboxllr.exe <command> [arguments]
       xboxllr.exe --framework <Git-checkout> <command> [arguments]

  title          Prepare, bind, configure, build or run a declared native title
  prepare        Low-level original-XBE translation/preparation driver
  dependencies   Package prepared renderer/UI/DSP dependency source
  python         Run a Python script or -m module with the bundled interpreter
  version        Print this toolkit's source identity

Use 'xboxllr.exe title --help' for the native build interface.
For Blinx's title-specific driver: xboxllr.exe python <BlinxRecomp>/tools/game.py --help
Read docs/BUILD-TOOL.md and docs/DEVELOPMENT.md before building.
MSVC, CMake, title metadata, prepared dependencies and your own original input
are still required. This is not a general-purpose playable Xbox emulator.
"""


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    framework = ROOT
    if argv and argv[0] == '--framework':
        if len(argv) < 3:
            raise ValueError('--framework requires a checkout and command')
        framework = Path(argv[1]).resolve(strict=True)
        argv = argv[2:]
    if not argv or argv[0] in ('-h', '--help', 'help'):
        print(HELP)
        return 0
    command = argv.pop(0)
    if command in ('version', '--version'):
        identity = ROOT / 'TOOLKIT-RELEASE.json'
        print(identity.read_text(encoding='utf-8') if identity.exists()
              else 'XboxLLR beta (source checkout)')
        return 0
    if command == 'python':
        if not argv:
            raise ValueError('python requires a script, -m module or -c expression')
        return subprocess.call([sys.executable, '-B', *argv])
    if command not in COMMANDS:
        raise ValueError('Unknown command: ' + command + '\nRun xboxllr.exe --help')
    script = framework / 'tools' / COMMANDS[command]
    sys.path.insert(0, str(script.parent))
    sys.argv = [str(script), *argv]
    runpy.run_path(str(script), run_name='__main__')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(2)
