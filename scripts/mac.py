#!/usr/bin/env python3
"""Build on the Mac mini over SSH (the `mac` host in ~/.ssh/config) and install on KaiC's iPhone.

    python scripts/mac.py phone [--debug] [--launch]   # build HEAD, install on the iPhone over Wi-Fi
    python scripts/mac.py build [--debug]              # the same build, stopping before the install

Everything runs headless in the Mac's hidden ~/.mac-dev: nothing opens on its screen. HEAD is pushed
to the Mac's own repo (the `mac` remote), never through GitHub, so uncommitted changes are left out.

The phone build is development-signed with the shared signing in ~/.mac-dev/signing (an App Store
Connect API key, a keychain of its own, no Apple ID) and One Four Seven's development profile. It
replaces the TestFlight install. Release by default, as TestFlight ships; --debug for the debug
export. Its build number is the clock (yymmddHHMM), and the phone's report of what it has installed
is printed, so a new build landing is checked, not assumed. The phone must be on the Mac's Wi-Fi.

The bought table model (game/licensed/, fitted by tools/fit_table.py and never in git) is copied
straight to the Mac's checkout over SSH when it differs from what the Mac has, so the phone gets
the table the PC shows. Without that folder the build draws the game's own table.

Nothing runs unless the Mac's sound is muted: a run that finds it unmuted stops before touching
anything and leaves the volume as it is.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = "com.kaichuan.onefourseven"
LICENSED = ROOT / "game" / "licensed"
STAMP = ".stamp"

MUTED = r"""
set -euo pipefail
# KaiC: our runs must never make a sound on the Mac, which someone else also uses. Its sound is
# left alone (not ours to change); a run only goes ahead while it is muted.
muted=$(osascript -e 'output muted of (get volume settings)' 2>&1 || true)
if [ "$muted" != true ]; then
  echo "stopped: the Mac's sound is not muted (output muted: $muted); nothing was run"
  exit 3
fi
"""

MAC_GAME = '"$HOME/.mac-dev/onefourseven/OneFourSeven/game"'

HAS_STAMP = MUTED + f"""
cat {MAC_GAME}/licensed/{STAMP} 2>/dev/null || true
"""

# Unpacked beside the old folder and swapped in, so a broken transfer leaves the old one whole.
UNPACK = f"""set -euo pipefail
game={MAC_GAME}
rm -rf "$game/licensed.new" && mkdir -p "$game/licensed.new"
tar -xf - -C "$game/licensed.new"
rm -rf "$game/licensed" && mv "$game/licensed.new" "$game/licensed"
"""

PREPARE = MUTED + r"""
export LANG=en_US.UTF-8 PATH="/opt/homebrew/bin:$HOME/.local/bin:$PATH"
APP_DIR="$HOME/.mac-dev/onefourseven"
LOGS="$APP_DIR/logs"
mkdir -p "$LOGS"
cd "$APP_DIR/OneFourSeven"
git fetch -q origin build
git checkout -q -f --detach FETCH_HEAD
git submodule update -q --init --depth 1
git log --oneline -1
started=$(date +%s)
step() { echo "$1 ($(( $(date +%s) - started )) s)"; }
fail() { tail -30 "$LOGS/$1.log"; echo "failed: $LOGS/$1.log on the Mac"; exit 1; }
py() { uv run -q --no-project --python 3.12 python "$@"; }
"""

BUILD = r"""
variant={variant}
export OFS_GODOT="$HOME/.mac-dev/tools/godot-4.7.1/Godot.app/Contents/MacOS/Godot"
# The game's user:// data from importing and exporting stays in ~/.mac-dev, not ~/Library.
export GODOT_HOME="$APP_DIR/godot-home"
mkdir -p "$GODOT_HOME"
godot() {{ HOME="$GODOT_HOME" nice -n 10 "$OFS_GODOT" "$@"; }}

nice -n 10 bash scripts/build_macos_ext.sh > "$LOGS/macos-ext.log" 2>&1 || fail macos-ext
step "macOS extension"
nice -n 10 bash scripts/build_ios_ext.sh "$variant" --device-only >"$LOGS/ios-ext.log" 2>&1 || fail ios-ext
step "iOS extension ($variant)"

py scripts/sync_data.py > "$LOGS/export.log" 2>&1 || fail export
py scripts/godot_tools.py templates >> "$LOGS/export.log" 2>&1 || fail export
rm -rf build/ios && mkdir -p build/ios
status=0
godot --headless --path game --import > "$LOGS/import.log" 2>&1 || status=1
godot --headless --path game --export-$variant "iOS" ../build/ios/OneFourSeven.xcodeproj >> "$LOGS/export.log" 2>&1 \
  || status=1
py scripts/godot_tools.py check-godot-log "$LOGS/import.log" "$LOGS/export.log" || status=1
py scripts/godot_tools.py ios-plist build/ios || status=1
[ $status -eq 0 ] || fail export
step "exported"

case $variant in debug) config=Debug ;; *) config=Release ;; esac
build_number=$(date +%y%m%d%H%M)
scheme=$(xcodebuild -list -json -project build/ios/OneFourSeven.xcodeproj \
  | py -c "import json, sys; print(json.load(sys.stdin)['project']['schemes'][0])")
# Unsigned here, signed below with the shared keychain, which stays off the login search list.
nice -n 10 xcodebuild -project build/ios/OneFourSeven.xcodeproj -scheme "$scheme" -configuration "$config" \
  -destination generic/platform=iOS -derivedDataPath build/xcode \
  CODE_SIGNING_ALLOWED=NO DEVELOPMENT_TEAM= CURRENT_PROJECT_VERSION="$build_number" \
  COMPILER_INDEX_STORE_ENABLE=NO build > "$LOGS/xcodebuild.log" 2>&1 || {{
    grep -E "error:" "$LOGS/xcodebuild.log" | head -20 || true; fail xcodebuild; }}
step "built $build_number"

set -a; . "$HOME/.mac-dev/signing/signing.env"; set +a
security unlock-keychain -p "$(cat "$HOME/.mac-dev/signing/keychain.pw")" "$KEYCHAIN"
app=$(ls -d build/xcode/Build/Products/$config-iphoneos/*.app | head -1)
profile=$(ls "$APP_DIR"/profiles/*.mobileprovision | head -1)
cp "$profile" "$app/embedded.mobileprovision"
security cms -D -i "$profile" > build/profile.plist
plutil -extract Entitlements xml1 -o build/entitlements.plist build/profile.plist
sign() {{ codesign --force --timestamp=none --keychain "$KEYCHAIN" --sign "$SIGN_SHA1" "$@" >> "$LOGS/sign.log" 2>&1; }}
: > "$LOGS/sign.log"
for item in "$app"/Frameworks/*; do
  [ -e "$item" ] && {{ sign "$item" || fail sign; }}
done
sign --entitlements build/entitlements.plist "$app" || fail sign
codesign --verify --strict "$app" >> "$LOGS/sign.log" 2>&1 || fail sign
step "signed"
"""

INSTALL = r"""
xcrun devicectl device install app --device "$PHONE_UDID" "$app" > "$LOGS/install.log" 2>&1 || fail install
installed=$(xcrun devicectl device info apps --device "$PHONE_UDID" --bundle-id {bundle} 2>/dev/null \
  | grep -oE "[0-9]+\.[0-9]+(\.[0-9]+)? +[0-9]+" | head -1)
step "installed on the iPhone: $installed (this build: $build_number)"
if [ "{launch}" = yes ]; then
  xcrun devicectl device process launch --device "$PHONE_UDID" {bundle} > "$LOGS/launch.log" 2>&1 || fail launch
  step "launched"
fi
"""


def push_head() -> None:
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT,
                           capture_output=True, text=True, check=True).stdout.strip()
    if dirty:
        print("note: uncommitted changes are not in this build", file=sys.stderr)
    subprocess.run(["git", "push", "-q", "--force", "mac", "HEAD:refs/heads/build"], cwd=ROOT, check=True)


def licensed_files() -> list[Path]:
    """What the Mac needs of game/licensed: not Godot's .import files, which it writes itself."""
    return sorted(f for f in LICENSED.rglob("*") if f.is_file() and f.suffix != ".import" and f.name != STAMP)


def licensed_stamp(files: list[Path]) -> str:
    digest = hashlib.sha256()
    for f in files:
        digest.update(f.relative_to(LICENSED).as_posix().encode() + b"\0")
        digest.update(hashlib.sha256(f.read_bytes()).digest())
    return digest.hexdigest()


def licensed_archive(files: list[Path], stamp: str) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as tar:
        for f in files:
            tar.add(f, arcname=f.relative_to(LICENSED).as_posix())
        info = tarfile.TarInfo(STAMP)
        info.size = len(stamp)
        tar.addfile(info, io.BytesIO(stamp.encode()))
    return buffer.getvalue()


def send_licensed() -> int:
    """Brings the Mac's game/licensed in line with this PC's; 0 when the build can go ahead."""
    files = licensed_files() if LICENSED.is_dir() else []
    if not files:
        print("note: no game/licensed here; the Mac builds with whatever table it already has", file=sys.stderr)
        return 0
    stamp = licensed_stamp(files)
    there = subprocess.run(["ssh", "-o", "BatchMode=yes", "mac", "bash", "-s"], input=HAS_STAMP.encode(),
                           capture_output=True)
    if there.returncode != 0:
        sys.stdout.write(there.stdout.decode(errors="replace"))
        sys.stderr.write(there.stderr.decode(errors="replace"))
        return there.returncode
    if there.stdout.decode(errors="replace").strip() == stamp:
        return 0
    archive = licensed_archive(files, stamp)
    print(f"sending the bought table to the Mac ({len(files)} files, {len(archive) / 1e6:.0f} MB)", flush=True)
    return subprocess.run(["ssh", "-o", "BatchMode=yes", "mac", UNPACK], input=archive).returncode


def on_mac(script: str) -> int:
    # Bytes, not text: on Windows a text pipe sends \r\n, and bash then reads `set -e\r`.
    return subprocess.run(["ssh", "-o", "BatchMode=yes", "mac", "bash", "-s"], input=script.encode()).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("phone", "build"):
        command = commands.add_parser(name)
        command.add_argument("--debug", action="store_true", help="the debug export instead of release")
    commands.choices["phone"].add_argument("--launch", action="store_true", help="open the app once installed")
    args = parser.parse_args()

    push_head()
    sent = send_licensed()
    if sent != 0:
        return sent
    script = PREPARE + BUILD.format(variant="debug" if args.debug else "release")
    if args.command == "phone":
        script += INSTALL.format(bundle=BUNDLE_ID, launch="yes" if args.launch else "no")
    return on_mac(script)


if __name__ == "__main__":
    sys.exit(main())
