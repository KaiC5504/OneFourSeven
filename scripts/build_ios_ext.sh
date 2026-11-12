#!/usr/bin/env bash
# Builds the extension for iPhone (arm64) and the simulator (arm64 + x86_64, the slices Godot's
# own simulator template has) and packages them as the .xcframework that
# game/bin/onefourseven.gdextension names. macOS with Xcode only.
#
#   bash scripts/build_ios_ext.sh <debug|release> [--device-only]
#
# --device-only is the Mac mini's phone build (scripts/mac.py): the iPhone slice alone, built
# incrementally on the last run's tree instead of from scratch.
set -euo pipefail

case "${1:-}" in
  debug) build_type=Debug ;;
  release) build_type=Release ;;
  *) echo "usage: $0 <debug|release> [--device-only]" >&2; exit 2 ;;
esac
device_only=no
case "${2:-}" in
  "") ;;
  --device-only) device_only=yes ;;
  *) echo "usage: $0 <debug|release> [--device-only]" >&2; exit 2 ;;
esac
target="template_$1"
root="$(cd "$(dirname "$0")/.." && pwd)"
name="libonefourseven.ios.$target"
out="$root/game/bin/$name.xcframework"
work="$root/build/ios-ext/$target"
toolchain="$root/thirdparty/godot-cpp/cmake/ios.toolchain.cmake"
jobs="$(sysctl -n hw.ncpu)"

# The extension must not ask for a newer iOS than the app it is embedded in.
min_ios="$(sed -n 's/^application\/min_ios_version="\(.*\)"$/\1/p' "$root/game/export_presets.cfg")"
if [ -z "$min_ios" ]; then
  echo "game/export_presets.cfg has no application/min_ios_version" >&2
  exit 1
fi

rm -rf "$out"
if [ "$device_only" = no ]; then
  rm -rf "$work"
fi
mkdir -p "$work"

# build_slice <dir> <ios.toolchain PLATFORM> <arch>: prints the built framework's new path.
build_slice() {
  local dir="$work/$1" platform=$2 arch=$3
  # ios.toolchain.cmake appends its flags to the cached ones on every configure, so a kept cache
  # changes every object's flags and rebuilds everything. A fresh cache keeps them identical.
  rm -f "$dir/build/CMakeCache.txt"
  cmake -S "$root" -B "$dir/build" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DPLATFORM="$platform" -DARCHS="$arch" \
    -DDEPLOYMENT_TARGET="$min_ios" -DCMAKE_BUILD_TYPE="$build_type" \
    -DGODOTCPP_TARGET="$target" -DOFS_BUILD_TESTS=OFF >&2
  cmake --build "$dir/build" --parallel "$jobs" >&2

  # Every slice is written to game/bin with an arch in its name, so move each one out before the
  # next build overwrites it. CMake drops the "lib" prefix for frameworks.
  shopt -s nullglob
  local built=("$root"/game/bin/onefourseven.ios."$target".*.framework)
  shopt -u nullglob
  if [ "${#built[@]}" -ne 1 ]; then
    echo "expected one framework in game/bin after the $1 build, found: ${built[*]:-none}" >&2
    exit 1
  fi
  rm -rf "$dir/$name.framework"
  mv "${built[0]}" "$dir/$name.framework"
  echo "$dir/$name.framework"
}

# Godot loads Frameworks/<xcframework basename>.framework/<same name> at run time, so the
# framework and its binary drop the arch from their names.
rename_framework() {
  local framework=$1 platforms=$2 executable
  executable="$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$framework/Info.plist")"
  if [ "$executable" != "$name" ]; then
    mv "$framework/$executable" "$framework/$name"
  fi
  install_name_tool -id "@rpath/$name.framework/$name" "$framework/$name"
  plutil -replace CFBundleExecutable -string "$name" "$framework/Info.plist"
  plutil -replace CFBundleName -string "$name" "$framework/Info.plist"
  plutil -replace CFBundlePackageType -string FMWK "$framework/Info.plist"
  # App Store validation rejects embedded frameworks without these, and CMake's template omits them.
  plutil -replace MinimumOSVersion -string "$min_ios" "$framework/Info.plist"
  plutil -replace CFBundleSupportedPlatforms -json "[\"$platforms\"]" "$framework/Info.plist"
}

device="$(build_slice device OS64 arm64)"
rename_framework "$device" iPhoneOS

if [ "$device_only" = yes ]; then
  lipo -info "$device/$name"
  if ! nm -gU "$device/$name" | grep -q "_ofs_library_init$"; then
    echo "$device/$name does not export ofs_library_init, the gdextension entry symbol" >&2
    exit 1
  fi
  xcodebuild -create-xcframework -framework "$device" -output "$out"
  echo "built $out (device only)"
  exit 0
fi

simulator="$(build_slice simulator-arm64 SIMULATORARM64 arm64)"
rename_framework "$simulator" iPhoneSimulator
simulator_x86="$(build_slice simulator-x86_64 SIMULATOR64 x86_64)"
rename_framework "$simulator_x86" iPhoneSimulator
lipo -create "$simulator/$name" "$simulator_x86/$name" -output "$simulator/$name.fat"
mv "$simulator/$name.fat" "$simulator/$name"

for framework in "$device" "$simulator"; do
  lipo -info "$framework/$name"
  if ! nm -gU "$framework/$name" | grep -q "_ofs_library_init$"; then
    echo "$framework/$name does not export ofs_library_init, the gdextension entry symbol" >&2
    exit 1
  fi
done

xcodebuild -create-xcframework -framework "$device" -framework "$simulator" -output "$out"
echo "built $out"
