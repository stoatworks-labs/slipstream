#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
MUTANTS=(
	"source/Shaders.cpp|float flux = FluxLaw > 0.5 ? 1.0 / ( rho * rho + z * z )|float flux = FluxLaw > 0.5 ? 1.0 / ( rho * rho - z * z )|--streak|GLSL: the star's flux, + -> -"
	"source/Shaders.cpp|- ceil( ( z - zc - dz ) / FIELD_DEPTH ) + 1.0;|- ceil( ( z - zc - dz ) / FIELD_DEPTH ) + 2.0;|--copies|GLSL: one copy of the column too many, 1 -> 2"
	"source/Shaders.cpp|vec3 e   = -log( max( vec3( 1.0 ) - lin, vec3( 1.0 / 4096.0 ) ) );|vec3 e   = +log( max( vec3( 1.0 ) - lin, vec3( 1.0 / 4096.0 ) ) );|--identity|GLSL: the clip's exposure, - -> +"
	"source/Shaders.cpp|sum += clipMirrored( Vanish + rel * ( 1.0 + du * ( float( i ) + 0.5 ) ), lod );|sum += clipMirrored( Vanish + rel * ( 1.0 - du * ( float( i ) + 0.5 ) ), lod );|--smear|GLSL: the smear's taps run in instead of out, + -> -"
	"source/Shaders.cpp|bool capture = ray.inward && ray.b < bCrit;|bool capture = ray.inward && ray.b > bCrit;|--shadow|GLSL: capture above the critical b, < -> >"
	"source/Shaders.cpp|float lambda = -ray.b * dot( no, DiscNormal ) * SpinSign;|float lambda = +ray.b * dot( no, DiscNormal ) * SpinSign;|--disc|GLSL: the photon's angular momentum, - -> +"
	"source/Shaders.cpp|float shift  = sqrt( 1.0 - 3.0 / rHit )|float shift  = sqrt( 1.0 - 2.0 / rHit )|--disc|GLSL: the orbiting emitter's time dilation, 3 -> 2"
	"source/Shaders.cpp|glow += 27.0 * u * speed * gg * gg;|glow += 27.0 * u * speed * gg + gg;|--ring|GLSL: the corona's g^4, * -> +"
	"source/Shaders.cpp|const float ESC_MAX  = 10.0;|const float ESC_MAX  = 11.0;|--einstein|GLSL: the table's row for an escaping b, 10 -> 11"
	"source/Flight.cpp|along[ i ] = along[ i - 1 ] + 0.5 * h * ( speed[ i - 1 ] + speed[ i ] );|along[ i ] = along[ i - 1 ] + 0.5 * h * ( speed[ i - 1 ] - speed[ i ] );|--clock|C++: the camera's trapezoids, + -> -"
	"source/Flight.cpp|const double rateIn  = 0.5 / std::max( in.entry, 1.0e-3 );|const double rateIn  = 0.6 / std::max( in.entry, 1.0e-3 );|--journey|C++: entry's rate, 0.5 -> 0.6"
	"source/Geodesic.cpp|return newtonian ? -u : -u + 3.0 * u * u;|return newtonian ? -u : -u + 2.0 * u * u;|--geodesic|C++: the orbit equation's 3u^2, 3 -> 2"
	"source/Blackbody.cpp|y = 0.821 * lobe( lambda, 568.8, 46.9, 40.5 )|y = 0.821 * lobe( lambda, 578.8, 46.9, 40.5 )|--planck|C++: y-bar's peak, 568.8 -> 578.8 nm"
)

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target sliptest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	if "$tree/build/sliptest" "$check" >"$WORK/log" 2>&1; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant:\n' "$check"
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
