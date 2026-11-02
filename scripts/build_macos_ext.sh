#!/usr/bin/env bash
# Builds the extension for macOS arm64 as the dylib game/bin/onefourseven.gdextension names under
# macos.debug.arm64. Only CI's headless macOS editor loads it: without it, --import and --export
# can't parse any script that uses an Ofs* class. No export ships it. macOS with Xcode only.
#
#   bash scripts/build_macos_ext.sh
#
# template_debug because the editor always has the "debug" feature, also for --export-release.
set -euo pipefail

if [ $# -ne 0 ]; then
  echo "usage: $0" >&2
  exit 2
fi
target=template_debug
root="$(cd "$(dirname "$0")/.." && pwd)"
name="libonefourseven.macos.$target.arm64.dylib"
out="$root/game/bin/$name"
work="$root/build/macos-ext"
jobs="$(sysctl -n hw.ncpu)"

rm -rf "$work" "$out"
cmake -S "$root" -B "$work" -G "Unix Makefiles" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Debug \
  -DGODOTCPP_TARGET="$target" -DOFS_BUILD_TESTS=OFF
cmake --build "$work" --parallel "$jobs" --target onefourseven

# godot-cpp names the file after CMAKE_SYSTEM_PROCESSOR, so an Intel runner would write x86_64.
if [ ! -f "$out" ]; then
  echo "the build wrote no $out; game/bin has:" >&2
  ls "$root/game/bin" >&2
  exit 1
fi
lipo -info "$out"
if [ "$(lipo -archs "$out")" != arm64 ]; then
  echo "$out is not arm64 only" >&2
  exit 1
fi
if ! nm -gU "$out" | grep -q "_ofs_library_init$"; then
  echo "$out does not export ofs_library_init, the gdextension entry symbol" >&2
  exit 1
fi
echo "built $out"
