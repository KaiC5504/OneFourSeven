"""Checks on the build helpers and the export presets that CI can only find out about on a
macOS runner: download hashes, the Info.plist fix-up, the preset's iOS keys, sync_data."""

import hashlib
import io
import json
import plistlib
import re
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import godot_tools  # noqa: E402
import sync_data  # noqa: E402

PRESETS = (ROOT / "game" / "export_presets.cfg").read_text(encoding="utf-8")
PROJECT = (ROOT / "game" / "project.godot").read_text(encoding="utf-8")
GDEXTENSION = (ROOT / "game" / "bin" / "onefourseven.gdextension").read_text(encoding="utf-8")
BUILD_IOS = (ROOT / "scripts" / "build_ios_ext.sh").read_text(encoding="utf-8")

# Keys Godot 4.7.1's iOS template Info.plist (misc/dist/apple_embedded_xcode) already writes.
TEMPLATE_KEYS = {"CFBundleDisplayName", "ITSAppUsesNonExemptEncryption", "UIRequiresFullScreen",
                 "UISupportedInterfaceOrientations", "UISupportedInterfaceOrientations~ipad",
                 "CFBundleVersion", "CFBundleShortVersionString", "CFBundleIdentifier"}


def ios_options():
    section = PRESETS.split("[preset.1.options]", 1)[1]
    options = {}
    for line in section.splitlines():
        match = re.match(r'^([\w/@]+)=(.*)$', line)
        if match:
            value = match.group(2)
            options[match.group(1)] = json.loads(value) if value.startswith('"') else value
    return options


def png(color_type):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, color_type, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IEND", b"")


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class Downloads(unittest.TestCase):
    def test_every_pinned_asset_has_a_full_sha512(self):
        self.assertEqual({"Godot_v4.7.1-stable_win64.exe.zip", "Godot_v4.7.1-stable_macos.universal.zip",
                          "Godot_v4.7.1-stable_export_templates.tpz"}, set(godot_tools.SHA512))
        for name, digest in godot_tools.SHA512.items():
            self.assertRegex(digest, r"^[0-9a-f]{128}$", name)

    def test_release_url_is_the_pinned_tag(self):
        self.assertEqual("https://github.com/godotengine/godot/releases/download/4.7.1-stable/",
                         godot_tools.RELEASE)

    def fetch_with(self, body, name="Godot_v4.7.1-stable_win64.exe.zip"):
        with tempfile.TemporaryDirectory() as temp, \
                mock.patch.object(godot_tools, "TOOLS", Path(temp)), \
                mock.patch.object(godot_tools.urllib.request, "urlopen", return_value=FakeResponse(body)), \
                mock.patch.dict(godot_tools.SHA512, {name: hashlib.sha512(b"the real zip").hexdigest()}):
            path = godot_tools.fetch(name)
            return path.read_bytes(), sorted(p.name for p in Path(temp).iterdir())

    def test_a_download_that_matches_is_kept(self):
        data, files = self.fetch_with(b"the real zip")
        self.assertEqual(b"the real zip", data)
        self.assertEqual(["Godot_v4.7.1-stable_win64.exe.zip"], files)

    def test_a_tampered_download_is_refused_and_removed(self):
        with self.assertRaises(SystemExit) as caught:
            self.fetch_with(b"something else")
        self.assertIn("sha512 mismatch", str(caught.exception))

    def test_templates_go_where_godot_looks(self):
        self.assertEqual("4.7.1.stable", godot_tools.templates_dir().name)
        self.assertEqual("export_templates", godot_tools.templates_dir().parent.name)


class IosPreset(unittest.TestCase):
    options = ios_options()

    def test_identity(self):
        self.assertEqual("com.kaichuan.onefourseven", self.options["application/bundle_identifier"])
        self.assertEqual("0.1.0", self.options["application/short_version"])
        self.assertEqual("0", self.options["application/targeted_device_family"])  # iPhone
        self.assertEqual("true", self.options["application/export_project_only"])
        self.assertRegex(self.options["application/app_store_team_id"], r"^0{10}$")

    def test_icon_is_the_1024_png(self):
        self.assertEqual("res://icons/icon.png", self.options["icons/icon_1024x1024"])

    def test_additional_plist_is_valid_and_repeats_no_template_key(self):
        content = self.options["application/additional_plist_content"]
        plist = plistlib.loads(f'<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>'
                               f'{content}</dict></plist>'.encode())
        self.assertEqual(["_ofs-devlink._tcp"], plist["NSBonjourServices"])
        self.assertTrue(plist["NSLocalNetworkUsageDescription"])
        self.assertFalse(TEMPLATE_KEYS & set(plist))
        self.assertEqual(len(re.findall(r"<key>", content)), len(plist))

    def test_project_orientation_is_sensor_landscape(self):
        self.assertIn("window/handheld/orientation=4", PROJECT)

    def test_home_screen_label_override_when_present(self):
        # The exporter takes CFBundleDisplayName from config/name read with the "ios" feature;
        # ios-plist fails CI if the exported value is not 147.
        match = re.search(r'^config/name\.ios="([^"]*)"', PROJECT, re.MULTILINE)
        if match:
            self.assertEqual(godot_tools.DISPLAY_NAME, match.group(1))

    def test_extension_targets_the_same_minimum_ios(self):
        self.assertRegex(self.options["application/min_ios_version"], r"^\d+\.\d+$")
        self.assertIn("application\\/min_ios_version", BUILD_IOS)

    def test_extension_names_match_the_gdextension(self):
        for kind in ("debug", "release"):
            self.assertIn(f'ios.{kind}="res://bin/libonefourseven.ios.template_{kind}.xcframework"',
                          GDEXTENSION)
        self.assertIn('name="libonefourseven.ios.$target"', BUILD_IOS)
        self.assertIn('target="template_$1"', BUILD_IOS)
        self.assertIn("-DGODOTCPP_TARGET=\"$target\"", BUILD_IOS)
        self.assertIn('out="$root/game/bin/$name.xcframework"', BUILD_IOS)


class IosPlistFix(unittest.TestCase):
    """fix_ios_export on a project shaped like Godot 4.7.1's export of this preset."""

    def make_export(self, root, *, display="147", icon=png(2), extra=None):
        (root / "OneFourSeven.xcodeproj" / "xcshareddata" / "xcschemes").mkdir(parents=True)
        (root / "OneFourSeven.xcodeproj" / "xcshareddata" / "xcschemes" / "OneFourSeven.xcscheme").write_text("")
        settings = ""
        for _ in range(2):  # Debug and Release
            settings += (f'PRODUCT_BUNDLE_IDENTIFIER = com.kaichuan.onefourseven;\n'
                         f'INFOPLIST_KEY_CFBundleDisplayName = "{display}";\n'
                         f'TARGETED_DEVICE_FAMILY = "1";\n')
        (root / "OneFourSeven.xcodeproj" / "project.pbxproj").write_text(settings)
        app = root / "OneFourSeven"
        (app / "Images.xcassets" / "AppIcon.appiconset").mkdir(parents=True)
        (app / "Images.xcassets" / "AppIcon.appiconset" / "Icon-1024.png").write_bytes(icon)
        info = {
            "CFBundleDisplayName": "$(INFOPLIST_KEY_CFBundleDisplayName)",
            "CFBundleShortVersionString": "$(MARKETING_VERSION)",
            "CFBundleVersion": "$(CURRENT_PROJECT_VERSION)",
            "ITSAppUsesNonExemptEncryption": False,
            "UIRequiresFullScreen": True,
            "UISupportedInterfaceOrientations": godot_tools.LANDSCAPE,
            "UISupportedInterfaceOrientations~ipad": godot_tools.LANDSCAPE,
            "NSLocalNetworkUsageDescription": "Finds the developer PC",
            "NSBonjourServices": ["_ofs-devlink._tcp"],
            **(extra or {}),
        }
        with (app / "OneFourSeven-Info.plist").open("wb") as f:
            plistlib.dump(info, f)
        return app / "OneFourSeven-Info.plist"

    def test_adds_all_four_ipad_orientations(self):
        with tempfile.TemporaryDirectory() as temp:
            plist = self.make_export(Path(temp))
            self.assertEqual([], godot_tools.fix_ios_export(Path(temp)))
            info = plistlib.loads(plist.read_bytes())
            self.assertEqual(4, len(set(info["UISupportedInterfaceOrientations~ipad"])))
            self.assertEqual(godot_tools.LANDSCAPE, info["UISupportedInterfaceOrientations"])

    def test_reports_each_missing_setting(self):
        with tempfile.TemporaryDirectory() as temp:
            self.make_export(Path(temp), display="One Four Seven", icon=png(6),
                             extra={"ITSAppUsesNonExemptEncryption": True,
                                    "UISupportedInterfaceOrientations": godot_tools.ALL_ORIENTATIONS})
            (Path(temp) / "OneFourSeven" / "helper.py").write_text("")
            problems = "\n".join(godot_tools.fix_ios_export(Path(temp)))
        for expected in ("INFOPLIST_KEY_CFBundleDisplayName", "alpha channel",
                         "ITSAppUsesNonExemptEncryption", "UISupportedInterfaceOrientations is",
                         "ITMS-90035"):
            self.assertIn(expected, problems)


class SyncData(unittest.TestCase):
    def test_copies_core_data_and_writes_build_info(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp)
            (target / "stale.json").write_text("{}")
            (target / "README.md").write_text("kept")
            with mock.patch.object(sync_data, "TARGET", target), \
                    mock.patch.dict("os.environ", {"OFS_BUILD_NUMBER": "42"}), \
                    mock.patch("sys.stdout", io.StringIO()):
                self.assertEqual(0, sync_data.main())
            names = {p.name for p in target.iterdir()}
            info = json.loads((target / "build_info.json").read_text())
        expected = {p.name for p in (ROOT / "core" / "data").glob("*.json")}
        self.assertEqual(expected | {"build_info.json", "README.md"}, names)
        self.assertEqual(re.search(r'^config/version="([^"]+)"', PROJECT, re.MULTILINE).group(1),
                         info["version"])
        self.assertEqual("42", info["build_number"])
        self.assertRegex(info["git_sha"], r"^[0-9a-f]{40}$")


if __name__ == "__main__":
    unittest.main()
