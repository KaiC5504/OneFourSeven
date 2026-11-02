#!/usr/bin/env bash
# Builds the extension for macOS arm64 as the dylib game/bin/onefourseven.gdextension names under
# macos.debug.arm64. Only CI's headless macOS editor loads it: without it, --import and --export
# can't parse any script that uses an Ofs* class. No export ships it. macOS with Xcode only.
#
#   bash scripts/build_macos_ext.sh
#
# template_debug because the editor always has the "debug" feature, also for --export-release.
#
# godot-cpp's part of the build tree is kept for the next build when that build has the same
# godot-cpp commit, toolchain and build files (the stamp below); CI caches it on the same. The
# rest of the tree and the dylib are built from the current sources every time.
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
godot_cpp="$root/thirdparty/godot-cpp"
godot_cpp_build="$work/gdext/godot-cpp"
stamp_file="$godot_cpp_build/ofs-build-stamp"
jobs="$(sysctl -n hw.ncpu)"

# What godot-cpp's objects are built from, beyond what make tracks once its sources are dated
# back. A modified submodule never matches.
godot_cpp_stamp() {
  if [ -n "$(git -C "$godot_cpp" status --porcelain)" ]; then
    echo "modified $(date +%s) $$"
    return
  fi
  git -C "$godot_cpp" rev-parse HEAD
  echo "$root"
  xcodebuild -version
  cmake --version | head -1
  (cd "$root" && shasum -a 256 scripts/build_macos_ext.sh CMakeLists.txt gdext/CMakeLists.txt)
}

stamp="$(godot_cpp_stamp)"
rm -f "$out"
if [ -f "$stamp_file" ] && [ "$(cat "$stamp_file")" = "$stamp" ]; then
  echo "keeping godot-cpp's objects in $godot_cpp_build"
  find "$work" -mindepth 1 -maxdepth 1 ! -name gdext -exec rm -rf {} +
  find "$work/gdext" -mindepth 1 -maxdepth 1 ! -name godot-cpp -exec rm -rf {} +
  # A checkout dates every godot-cpp file after the kept objects, so make would rebuild them all.
  # The stamp has matched the commit they were built from, so its sources are dated back instead.
  find "$godot_cpp" -path "$godot_cpp/.git" -prune -o -exec touch -h -t 200001010000 {} +
else
  if [ -f "$stamp_file" ]; then
    printf 'rebuilding godot-cpp: its objects were built from\n%s\nnot\n%s\n' "$(cat "$stamp_file")" "$stamp"
  fi
  rm -rf "$work"
fi

cmake -S "$root" -B "$work" -G "Unix Makefiles" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Debug \
  -DGODOTCPP_TARGET="$target" -DOFS_BUILD_TESTS=OFF
cmake --build "$work" --parallel "$jobs" --target onefourseven
printf '%s\n' "$stamp" > "$stamp_file"

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
