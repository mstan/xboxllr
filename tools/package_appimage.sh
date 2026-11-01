#!/usr/bin/env bash
# Package a native_linux_package.py payload. Run on a Linux filesystem: DrvFs
# does not reliably retain the AppDir's executable bits and library symlinks.
set -euo pipefail
payload=; output=; stage=; tools=; library_dirs=()
while (($#)); do
    case "$1" in
        --payload) payload=$2; shift 2;;
        --output) output=$2; shift 2;;
        --stage) stage=$2; shift 2;;
        --tools) tools=$2; shift 2;;
        --library-dir) library_dirs+=("$2"); shift 2;;
        *) echo "Usage: $0 --payload DIR --output FILE.AppImage --stage NEW_DIR --tools CACHE [--library-dir DIR ...]" >&2; exit 2;;
    esac
done
[[ -n "$payload" && -n "$output" && -n "$stage" && -n "$tools" ]] || { echo 'Missing required packaging paths' >&2; exit 2; }
[[ ! -e "$stage" && ! -e "$output" ]] || { echo 'Choose new stage/output paths; existing data is preserved' >&2; exit 2; }
[[ "$stage" != /mnt/* ]] || { echo 'Stage AppDir on a native Linux filesystem' >&2; exit 2; }
mkdir -p "$tools" "$stage" "$(dirname "$output")"
tools=$(realpath "$tools")
appdir=$(realpath "$stage")/AppDir
mkdir -p "$appdir/usr/share/xboxllr" "$appdir/usr/lib/python" "$appdir/usr/bin"
cp -a "$payload/." "$appdir/usr/share/xboxllr/"
fetch() {
    local url=$1 sha=$2 dest=$3
    if [[ ! -f "$dest" ]] || ! printf '%s  %s\n' "$sha" "$dest" | sha256sum -c --status; then
        curl -fL --retry 3 "$url" -o "$dest.download"
        printf '%s  %s\n' "$sha" "$dest.download" | sha256sum -c -
        mv "$dest.download" "$dest"
    fi
}
fetch 'https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage' \
    d900b99faf8656aa613038dafbd2cc23ca7f90794a9a00c9546f0ab572af94dd "$tools/linuxdeploy.AppImage"
fetch 'https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage' \
    657307e89c176267fe6ae8c00bc1f0a849222fabdde593dd50b116248c9fce89 "$tools/appimagetool.AppImage"
fetch 'https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz' \
    ce4f6d0a6cfdb3bed6e44c2f9affd85943c76a42e25ad3a259002f31d6fb8a20 "$tools/python.tar.gz"
chmod +x "$tools/linuxdeploy.AppImage" "$tools/appimagetool.AppImage"
tar -xzf "$tools/python.tar.gz" --strip-components=1 -C "$appdir/usr/lib/python"
IFS=:; export LD_LIBRARY_PATH="${library_dirs[*]}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; unset IFS
python="$appdir/usr/lib/python/bin/python3"
app_id=$("$python" -I -c 'import json,sys;print(json.load(open(sys.argv[1]))["application_id"])' "$payload/release.json")
name=$("$python" -I -c 'import json,sys;print(json.load(open(sys.argv[1]))["title"]["name"])' "$payload/release.json")
cat > "$appdir/$app_id.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$name
Exec=AppRun
Icon=$app_id
Categories=Game;
Terminal=false
EOF
cat > "$appdir/$app_id.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"><rect width="256" height="256" rx="52" fill="#122010"/><circle cx="128" cy="128" r="94" fill="#79c630"/><path d="M70 56L128 102L186 56L155 127L199 201L128 156L57 201L101 127Z" fill="#122010"/></svg>
EOF
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
set -eu
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export APPDIR
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/python/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONDONTWRITEBYTECODE=1
exec "$APPDIR/usr/lib/python/bin/python3" -I "$APPDIR/usr/share/xboxllr/tools/native_linux_entry.py" --payload "$APPDIR/usr/share/xboxllr" "$@"
EOF
chmod +x "$appdir/AppRun"
args=(--appimage-extract-and-run --appdir "$appdir" --desktop-file "$appdir/$app_id.desktop" --icon-file "$appdir/$app_id.svg")
while IFS= read -r -d '' binary; do
    if [[ $(head -c 4 "$binary") == $'\x7fELF' ]]; then
        args+=(--library "$binary")
    fi
done < <(find "$appdir/usr/share/xboxllr/native" "$appdir/usr/lib/python" -type f -print0)
# --library walks dependencies without treating the title/launcher as the
# AppImage entry point; AppRun owns Python, seeding and the session lifetime.
"$tools/linuxdeploy.AppImage" "${args[@]}"
# Dependency deployment can strip/relocate an ELF. Hash the final packaged
# files, while retaining the original build hashes separately in `builds`.
"$python" -I - "$appdir/usr/share/xboxllr" <<'PY'
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1]); path=root/'release.json'
record=json.loads(path.read_text())
for name in record['files']:
    with (root/name).open('rb') as stream:
        record['files'][name]=hashlib.file_digest(stream,'sha256').hexdigest()
path.write_text(json.dumps(record,indent=2)+'\n')
PY
ARCH=x86_64 "$tools/appimagetool.AppImage" --appimage-extract-and-run "$appdir" "$output"
sha256sum "$output"
