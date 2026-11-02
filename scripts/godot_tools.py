#!/usr/bin/env python3
"""The pinned Godot editor and export templates, downloaded and checked against hashes kept here.

    python scripts/godot_tools.py editor      # prints the editor executable (last line)
    python scripts/godot_tools.py templates   # prints the installed templates directory
    python scripts/godot_tools.py ios-plist build/ios   # fixes up and checks an exported Xcode project
    python scripts/godot_tools.py check-godot-log import.log export.log   # fails on script errors

The hashes come from the 4.7.1-stable release's SHA512-SUMS.txt, read once and committed, so a
tampered download or sums file fails here instead of reaching a build.
"""

from __future__ import annotations

import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "build" / "tools" / "godot"
VERSION = "4.7.1"
RELEASE = f"https://github.com/godotengine/godot/releases/download/{VERSION}-stable/"
TEMPLATES_VERSION = f"{VERSION}.stable"
LOCAL_EDITOR = Path("D:/Tools/Godot/Godot_v4.7.1-stable_win64_console.exe")

SHA512 = {
    "Godot_v4.7.1-stable_win64.exe.zip":
        "a6b02c527c18ba9936e63562032701432b2dc57d98d6483ceaccb00fe14af16af5773ae8a55e7b4d614edf121c4d9e420d870f804edb1dac16362298a01ce6c4",
    "Godot_v4.7.1-stable_macos.universal.zip":
        "a5c6443e193829de9a3237b57ef5e01c23839888900e241543da0dd4bac1050125e19469f0cca9a9958ac346070d98cab8e5d6aee16b181c6d06cda86bd07224",
    "Godot_v4.7.1-stable_export_templates.tpz":
        "afcc83d8d3d298038f19c58744a0d660fa75dd4baa33cb55d1011bb2565a2a8c2381728924564cb909e37c205a23f21b521b23bd057993afd43ae4da0b2f9d47",
}

BUNDLE_ID = "com.kaichuan.onefourseven"
DISPLAY_NAME = "147"
LANDSCAPE = ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"]
ALL_ORIENTATIONS = LANDSCAPE + ["UIInterfaceOrientationPortrait", "UIInterfaceOrientationPortraitUpsideDown"]
BONJOUR = ["_ofs-devlink._tcp"]
# Only CI's macOS editor loads this build of the extension; an iOS export must never carry it.
MACOS_EXTENSION = "onefourseven.macos."

# Lines in editor output that mean a script or the extension failed to load. Godot still exits 0
# and the export still runs, so without this they only show up as noise in the log.
GODOT_LOG_PROBLEMS = ("SCRIPT ERROR", "Parse Error", "Failed to load script", "Failed to create an autoload",
                      "No GDExtension library found", "GDExtension dynamic library not found",
                      "Error loading extension", "Can't open dynamic library",
                      "GDExtension entry point '", "GDExtension initialization function '")
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
# Godot prints a script error's location on a later "at:" line, with progress output between.
AT_LINE = re.compile(r"^\s*at: ")
SCRIPT_LOCATION = re.compile(r"\((res://[^()]*:\d+)\)\s*$")


def log(message: str) -> None:
    # stdout's last line is the answer callers capture, so progress goes to stderr.
    print(message, file=sys.stderr, flush=True)


def sha512_of(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(name: str) -> Path:
    """The release asset in build/tools/godot/, downloaded if missing, always hash-checked."""
    want = SHA512[name]
    target = TOOLS / name
    if target.exists():
        if sha512_of(target) == want:
            return target
        log(f"{target} does not match its pinned sha512; downloading again")
        target.unlink()
    TOOLS.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    log(f"downloading {RELEASE}{name}")
    digest = hashlib.sha512()
    with urllib.request.urlopen(RELEASE + name, timeout=60) as response, partial.open("wb") as out:
        for block in iter(lambda: response.read(1 << 20), b""):
            digest.update(block)
            out.write(block)
    got = digest.hexdigest()
    if got != want:
        partial.unlink()
        raise SystemExit(f"{name}: sha512 mismatch\n  want {want}\n  got  {got}")
    partial.replace(target)
    return target


def editor_name() -> tuple[str, str]:
    """(release asset, executable path inside the extracted folder) for this OS."""
    if sys.platform == "win32":
        return "Godot_v4.7.1-stable_win64.exe.zip", "Godot_v4.7.1-stable_win64_console.exe"
    if sys.platform == "darwin":
        return "Godot_v4.7.1-stable_macos.universal.zip", "Godot.app/Contents/MacOS/Godot"
    raise SystemExit(f"no pinned Godot editor for {sys.platform}")


def ensure_editor() -> Path:
    asset, executable = editor_name()
    folder = TOOLS / asset.removesuffix(".zip")
    exe = folder / executable
    stamp = folder / ".sha512"
    if exe.exists() and stamp.exists() and stamp.read_text().strip() == SHA512[asset]:
        return exe
    archive = fetch(asset)
    shutil.rmtree(folder, ignore_errors=True)
    log(f"extracting {archive.name}")
    if sys.platform == "darwin":
        # zipfile drops the symlinks and exec bits inside Godot.app.
        subprocess.run(["ditto", "-x", "-k", str(archive), str(folder)], check=True)
    else:
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    if not exe.exists():
        raise SystemExit(f"{asset} has no {executable}")
    stamp.write_text(SHA512[asset] + "\n")
    return exe


def find_godot() -> Path:
    """The editor for local scripts: $OFS_GODOT, else the PC's install, else the pinned download."""
    configured = os.environ.get("OFS_GODOT")
    if configured:
        path = Path(configured)
        if not path.exists():
            raise SystemExit(f"OFS_GODOT={configured} does not exist")
        return path
    if sys.platform == "win32" and LOCAL_EDITOR.exists():
        return LOCAL_EDITOR
    return ensure_editor()


def templates_dir() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ["APPDATA"]) / "Godot"
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support" / "Godot"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "godot"
    return base / "export_templates" / TEMPLATES_VERSION


def ensure_templates() -> Path:
    asset = "Godot_v4.7.1-stable_export_templates.tpz"
    target = templates_dir()
    stamp = target / ".ofs-sha512"
    if stamp.exists() and stamp.read_text().strip() == SHA512[asset]:
        return target
    archive = fetch(asset)
    log(f"installing {archive.name} into {target}")
    with zipfile.ZipFile(archive) as z:
        version = z.read("templates/version.txt").decode().strip()
        if version != TEMPLATES_VERSION:
            raise SystemExit(f"{asset} is for {version}, not {TEMPLATES_VERSION}")
        shutil.rmtree(target, ignore_errors=True)
        target.mkdir(parents=True)
        for info in z.infolist():
            name = info.filename.removeprefix("templates/")
            if info.is_dir() or not name or ".." in Path(name).parts:
                continue
            out = target / name
            out.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, out.open("wb") as dst:
                shutil.copyfileobj(src, dst, 1 << 20)
    stamp.write_text(SHA512[asset] + "\n")
    return target


def pbx_object(pbxproj: str, object_id: str) -> str:
    """The text between the braces of one object in a project.pbxproj, or "" if it is missing."""
    start = re.search(rf"^\s*{object_id}\b[^=\n]*= \{{", pbxproj, re.MULTILINE)
    if not start:
        return ""
    depth = 0
    for i in range(start.end() - 1, len(pbxproj)):
        if pbxproj[i] == "{":
            depth += 1
        elif pbxproj[i] == "}":
            depth -= 1
            if depth == 0:
                return pbxproj[start.end():i]
    return ""


def app_target_configs(pbxproj: str) -> dict[str, str]:
    """The application target's build configurations, by name (Debug, Release)."""
    configs = {}
    for target in re.finditer(r"^\s*([0-9A-F]{24})\b[^=\n]*= \{\s*isa = PBXNativeTarget;", pbxproj,
                              re.MULTILINE):
        body = pbx_object(pbxproj, target.group(1))
        if 'productType = "com.apple.product-type.application";' not in body:
            continue
        listed = re.search(r"buildConfigurationList = ([0-9A-F]{24})", body)
        members = re.search(r"buildConfigurations = \(([^)]*)\)",
                            pbx_object(pbxproj, listed.group(1)) if listed else "")
        for config_id in re.findall(r"\b[0-9A-F]{24}\b", members.group(1) if members else ""):
            config = pbx_object(pbxproj, config_id)
            name = re.search(r"^\s*name = \"?([^\";]*)\"?;", config, re.MULTILINE)
            configs[name.group(1) if name else config_id] = config
    return configs


def fix_ios_export(export_dir: Path) -> list[str]:
    """Gives the exported Info.plist all four iPad orientations and checks every pipeline key.

    Godot writes UISupportedInterfaceOrientations~ipad from the project orientation (two entries
    for sensor landscape), but upload validation wants all four even for an iPhone-only app. Doing
    it here instead of in additional_plist_content keeps the plist free of duplicate keys.
    Returns the problems found; empty means the project is fit to build.
    """
    projects = sorted(export_dir.glob("*.xcodeproj"))
    if len(projects) != 1:
        return [f"expected one .xcodeproj in {export_dir}, found {[p.name for p in projects]}"]
    name = projects[0].stem
    plist_path = export_dir / name / f"{name}-Info.plist"
    pbxproj = (projects[0] / "project.pbxproj").read_text(encoding="utf-8")
    with plist_path.open("rb") as f:
        info = plistlib.load(f)
    info["UISupportedInterfaceOrientations~ipad"] = ALL_ORIENTATIONS
    with plist_path.open("wb") as f:
        plistlib.dump(info, f)

    problems = []

    def expect(key: str, want: object) -> None:
        if info.get(key) != want:
            problems.append(f"Info.plist {key} is {info.get(key)!r}, want {want!r}")

    if sorted(info.get("UISupportedInterfaceOrientations", [])) != sorted(LANDSCAPE):
        problems.append(f"Info.plist UISupportedInterfaceOrientations is "
                        f"{info.get('UISupportedInterfaceOrientations')!r}, want both landscapes only")
    expect("ITSAppUsesNonExemptEncryption", False)
    expect("NSBonjourServices", BONJOUR)
    if not info.get("NSLocalNetworkUsageDescription"):
        problems.append("Info.plist has no NSLocalNetworkUsageDescription")
    expect("CFBundleShortVersionString", "$(MARKETING_VERSION)")
    expect("CFBundleVersion", "$(CURRENT_PROJECT_VERSION)")
    expect("CFBundleDisplayName", "$(INFOPLIST_KEY_CFBundleDisplayName)")
    for setting, want in (("PRODUCT_BUNDLE_IDENTIFIER", BUNDLE_ID),
                          ("INFOPLIST_KEY_CFBundleDisplayName", DISPLAY_NAME),
                          ("TARGETED_DEVICE_FAMILY", "1")):
        found = set(re.findall(rf"\b{setting} = \"?([^\";]*)\"?;", pbxproj))
        if found != {want}:
            problems.append(f"project.pbxproj {setting} is {sorted(found)}, want {want!r}")
    # Pipeline-critical in the ios-testflight skill; only the app target's own setting counts.
    configs = app_target_configs(pbxproj)
    if not configs:
        problems.append("project.pbxproj has no application target with build configurations")
    for config_name, config in sorted(configs.items()):
        style = re.findall(r"\bCODE_SIGN_STYLE = \"?([^\";]*)\"?;", config)
        if style != ["Automatic"]:
            problems.append(f"project.pbxproj app target {config_name} CODE_SIGN_STYLE is {style}, "
                            "want ['Automatic']")
    if not list(export_dir.glob(f"{name}.xcodeproj/xcshareddata/xcschemes/*.xcscheme")):
        problems.append("no shared scheme in the exported project")
    icon = export_dir / name / "Images.xcassets" / "AppIcon.appiconset"
    icons = sorted(icon.glob("*.png"))
    if not icons:
        problems.append(f"no app icon in {icon}")
    for png in icons:
        # IHDR colour types 4 and 6 carry alpha, which App Store Connect rejects on the 1024 icon.
        if png.read_bytes()[25:26] in (b"\x04", b"\x06"):
            problems.append(f"{png.relative_to(export_dir)} has an alpha channel")
    bundled = [str(p.relative_to(export_dir)) for p in export_dir.rglob("*")
               if p.suffix in (".py", ".sh") and p.is_file()]
    if bundled:
        problems.append(f"scripts would ship as resources (ITMS-90035): {bundled}")
    macos = [str(p.relative_to(export_dir)) for p in export_dir.rglob("*") if MACOS_EXTENSION in p.name]
    if MACOS_EXTENSION in pbxproj:
        macos.append(f"{name}.xcodeproj/project.pbxproj")
    if macos:
        problems.append(f"the macOS editor's extension is in the iOS export: {sorted(macos)}")
    return problems


def godot_log_problems(path: Path) -> list[str]:
    """Every line of a Godot editor log that shows a script or the extension failed to load, as
    file:line, with the res:// location Godot gives a script error. A missing or empty log counts."""
    if not path.is_file():
        return [f"{path.as_posix()}: no such log"]
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = [ANSI_ESCAPE.sub("", line).rstrip("\r") for line in text.split("\n")]
    if not any(line.strip() for line in lines):
        return [f"{path.as_posix()}: empty; Godot printed nothing"]
    problems = []
    for number, line in enumerate(lines, 1):
        if not any(marker in line for marker in GODOT_LOG_PROBLEMS):
            continue
        where = ""
        if "SCRIPT ERROR" in line:
            at = next((later for later in lines[number:number + 8] if AT_LINE.match(later)), "")
            location = SCRIPT_LOCATION.search(at)
            if location:
                where = f" ({location.group(1)})"
        problems.append(f"{path.as_posix()}:{number}: {line.strip()}{where}")
    return problems


def main(argv: list[str]) -> int:
    if argv[:1] == ["editor"] and len(argv) == 1:
        print(ensure_editor())
    elif argv[:1] == ["templates"] and len(argv) == 1:
        print(ensure_templates())
    elif argv[:1] == ["ios-plist"] and len(argv) == 2:
        problems = fix_ios_export(Path(argv[1]))
        for problem in problems:
            print(f"error: {problem}")
        if problems:
            return 1
        print(f"{argv[1]}: Info.plist and build settings ok")
    elif argv[:1] == ["check-godot-log"] and len(argv) >= 2:
        problems = [problem for log_file in argv[1:] for problem in godot_log_problems(Path(log_file))]
        for problem in problems:
            print(f"error: {problem}")
        if problems:
            print(f"error: {len(problems)} script or extension problems in {', '.join(argv[1:])}")
            return 1
        print(f"{', '.join(argv[1:])}: no script or extension errors")
    else:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
