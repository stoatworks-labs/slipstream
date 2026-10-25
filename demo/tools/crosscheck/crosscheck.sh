#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, against the plugin's C++.
#
#     demo/tools/crosscheck/crosscheck.sh        exit 1 on any difference
#
# Two checks, each a C++ program and a JavaScript one that print the same thing:
#
#   1. THE PORT. printer.cpp drives the plugin's own GL-free sources --
#      Controls.cpp, Flight.cpp, Blackbody.cpp, Geodesic.cpp -- through a fixed
#      scenario: every slider position's mapping and inverse, every look's script
#      across J, the shadow and its inverse, every drive at 60, 50 and 30 fps
#      through jumps abandoned halfway and a fader scrubbed both ways, the camera,
#      every whole-frame Transit Time of a Trip (the first trap in AGENTS.md), the
#      blackbody table and both orbit tables. run.mjs runs the PORT block of
#      demo/plugin.js through the same.
#   2. THE FRAMES. uniforms.cpp drives the REAL plugin class (Slipstream.cpp and
#      everything it calls, with the FFGL SDK) in a headless GL 4.1 context
#      through scenario.mjs's 9,864 frames of both plugins, and after every frame
#      reads every active uniform back out of its four programs. frames.mjs runs
#      the FRAME block (ProcessOpenGL's CPU half) through the same frames.
#
# Floats and doubles print as their bits. What has to be identical, and why it
# is measured rather than assumed:
#
#   - THE LIBM IS TAKEN OUT. JavaScript's Math.exp and friends are V8's, the
#     plugin's are Apple's libm, and they disagree in the last bit of about one
#     distinct result in ten (this script prints the count). So each C++ program
#     runs with libm_record.c interposed, which writes down every libm call it
#     makes, and the JavaScript answers its Math calls from that record
#     (replay.mjs). With that, and with the C++ built -ffp-contract=off (see
#     below), the port must print EXACTLY what the plugin prints: every line of
#     check 1 and every CPU double and every uniform of check 2.
#   - THE PAGE'S OWN NUMBERS are then measured, not required: the port on V8's
#     Math against the plugin, in ulps. The GPU-facing tables (the blackbody
#     texels, the orbit tables' floats) must still be identical to the bit.
#   - THE FUSED MULTIPLY-ADD. Apple clang's default -ffp-contract=on fuses a*b+c
#     on arm64, which JavaScript cannot do, so the plugin's Apple Silicon slice
#     rounds differently from its x86_64 slice (whose baseline has no FMA) and
#     from this port. -ffp-contract=off is the x86_64 slice's arithmetic and is
#     what must match. The native build is compared too, and reported.
#
# Run by tools/verify.sh. Needs node, a C and C++ compiler, the FFGL submodule
# and an OpenGL context (check 2 drives the plugin's shaders for real).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"
CC="${CC:-cc}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }
[[ -f "$REPO/external/ffgl/CMakeLists.txt" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
port_sources=( "$REPO/source/Controls.cpp" "$REPO/source/Flight.cpp" "$REPO/source/Blackbody.cpp" "$REPO/source/Geodesic.cpp" )
plugin_sources=( "$REPO/source/Diag.cpp" "${port_sources[@]}" "$REPO/source/PassBuffer.cpp" "$REPO/source/Shaders.cpp"
                 "$REPO/source/Slipstream.cpp" "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" )
plugin_flags=( -std=c++17 -O3 -DGL_SILENCE_DEPRECATION -DSLIPSTREAM_VERSION="\"$version\"" -I "$REPO/source"
               -I "$REPO/external/ffgl/source/lib" -framework OpenGL -framework AppKit -w )

"$CC" -O2 -fno-builtin -dynamiclib "$HERE/libm_record.c" -o "$tmp/librecord.dylib"
"$CXX" -std=c++17 -O3 -ffp-contract=off -I "$REPO/source" "$HERE/printer.cpp" "${port_sources[@]}" -o "$tmp/printer-strict"
"$CXX" -std=c++17 -O3 -I "$REPO/source" "$HERE/printer.cpp" "${port_sources[@]}" -o "$tmp/printer-native"
"$CXX" -ffp-contract=off "${plugin_flags[@]}" "$HERE/uniforms.cpp" "${plugin_sources[@]}" -o "$tmp/uniforms-strict"
"$CXX" "${plugin_flags[@]}" "$HERE/uniforms.cpp" "${plugin_sources[@]}" -o "$tmp/uniforms-native"

recorded() {
	# A dyld that ignored DYLD_INSERT_LIBRARIES would leave no record, and the
	# replay would silently be V8's Math again.
	[[ -s "$1" ]] || { echo "FAIL the libm recorder wrote nothing to $(basename "$1") -- was DYLD_INSERT_LIBRARIES ignored?"; exit 1; }
}
summary() {
	# replay.mjs's line: how many calls, which fell back, and V8 against the libm.
	python3 - "$1" <<'PY'
import json, sys
s = json.loads(open(sys.argv[1]).read().strip().splitlines()[-1])
miss = s.get("misses", {})
print(f"     {s['replayed']:,} libm calls recorded and replayed;", end=" ")
print("none fell back" if not miss else
      "fell back to V8 (arguments clang folded at build time): " + ", ".join(f"{k} {v:,}" for k, v in sorted(miss.items())))
if "libm" in s:
    parts = [f"{k} {v['differ']:,} of {v['distinct']:,}" for k, v in s["libm"].items()]
    worst = max(v["worstUlps"] for v in s["libm"].values())
    print(f"     V8's Math against Apple's libm, distinct calls differing (worst {worst} ulp): " + ", ".join(parts))
PY
}

#---------------------------------------------------------------------------
# 1. The port.
#---------------------------------------------------------------------------
DYLD_INSERT_LIBRARIES="$tmp/librecord.dylib" LIBM_RECORD="$tmp/port-calls.bin" "$tmp/printer-strict" > "$tmp/port-cpp.txt"
recorded "$tmp/port-calls.bin"
TABLES_OUT="$tmp/tables-native.f32" "$tmp/printer-native" > "$tmp/port-cpp-native.txt"
LIBM_REPLAY="$tmp/port-calls.bin" node "$HERE/run.mjs" > "$tmp/port-js-replay.txt" 2> "$tmp/port-js-replay.err"
TABLES_OUT="$tmp/tables-js.f32" node "$HERE/run.mjs" > "$tmp/port-js.txt"

lines=$( wc -l < "$tmp/port-js-replay.txt" | tr -d ' ' )
[[ "$lines" -gt 80000 ]] || { echo "FAIL the JavaScript side printed only $lines lines"; exit 1; }
if ! out=$( python3 "$HERE/compare.py" exact "$tmp/port-cpp.txt" "$tmp/port-js-replay.txt" ); then
	echo "FAIL the JavaScript port differs from the plugin's Controls/Flight/Blackbody/Geodesic.cpp (libm replayed, contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the port: $lines lines identical to the plugin's C++, libm replayed, contraction off (the x86_64 slice's arithmetic)"
summary "$tmp/port-js-replay.err"

if ! out=$( python3 "$HERE/compare.py" ulps "$tmp/port-cpp.txt" "$tmp/port-js.txt" --tables ); then
	printf '%s\n' "$out" | sed 's/^/   /'
	echo "FAIL on V8's own Math the port gives the GPU a different table texel"
	exit 1
fi
echo "ok   the port on V8's own Math, as the page runs it: every blackbody and orbit-table float the GPU reads identical; the doubles:"
printf '%s\n' "$out" | grep -v ' identical$' | sed 's/^/  /' || true
echo "     against the plugin as built for arm64 (fused multiply-adds), reported, not required:"
python3 "$HERE/compare.py" ulps "$tmp/port-cpp-native.txt" "$tmp/port-js.txt" 2>&1 | grep -v ' identical$' | sed 's/^/  /' || true
python3 "$HERE/compare.py" tables "$tmp/tables-native.f32" "$tmp/tables-js.f32" | sed 's/^/  /' 

#---------------------------------------------------------------------------
# 2. The frames.
#---------------------------------------------------------------------------
node "$HERE/scenario.mjs" > "$tmp/scenario.txt"
DYLD_INSERT_LIBRARIES="$tmp/librecord.dylib" LIBM_RECORD="$tmp/frame-calls.bin" "$tmp/uniforms-strict" < "$tmp/scenario.txt" > "$tmp/frames-cpp.txt"
recorded "$tmp/frame-calls.bin"
"$tmp/uniforms-native" < "$tmp/scenario.txt" > "$tmp/frames-cpp-native.txt"
LIBM_REPLAY="$tmp/frame-calls.bin" node "$HERE/frames.mjs" < "$tmp/scenario.txt" > "$tmp/frames-js-replay.txt" 2> "$tmp/frames-js-replay.err"
node "$HERE/frames.mjs" < "$tmp/scenario.txt" > "$tmp/frames-js.txt"

if ! out=$( python3 "$HERE/compare.py" frames "$tmp/frames-cpp.txt" "$tmp/frames-js-replay.txt" ); then
	echo "FAIL the JavaScript frame logic differs from the real plugin's (libm replayed, contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the frames, the real plugin class against the FRAME block, libm replayed, contraction off: $out"
summary "$tmp/frames-js-replay.err"
echo "     on V8's own Math, as the page runs it, against that plugin:"
python3 "$HERE/compare.py" frames-report "$tmp/frames-cpp.txt" "$tmp/frames-js.txt" | sed 's/^/  /'
echo "     and against the plugin as built for arm64:"
python3 "$HERE/compare.py" frames-report "$tmp/frames-cpp-native.txt" "$tmp/frames-js.txt" | sed 's/^/  /'
