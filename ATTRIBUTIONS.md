# Attributions

Slipstream is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

**Provisional hand copy (2026-10-25).** The fleet's attributions files are generated: the
master lists live in the `stoatworks-backend` repo and are pushed out by
`scripts/sync-attributions.py`. Slipstream isn't registered there yet, so this one was
written by hand in the same shape. Once it is registered, the generated file replaces
this one and the entries below move into the backend's lists.

## Code derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling, two plugins from one core — Stoatworks langton

<https://github.com/stoatworks-labs/langton>  
Licence: MIT  
Copyright: Stoatworks Labs

`CMakeLists.txt`'s two bundles from one OBJECT library, the plugin class's shape (a host
order per plugin with the About block last, the clock that votes on seconds against
milliseconds), the harness's rig, PNG writer, cue sheets, `--pipe`/`--film`,
`--negative` with its Perturb, `--state`, `--names`, `--cues` and `--offline`;
`tools/verify.sh`, `tools/mutate.sh`, `tools/glslc.sh`, `tools/sweep.py`, the CI and
release workflows and `scripts/release-lib.sh`. All adapted.

### The pass buffers, the GL state and the log — Stoatworks langton, millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

`source/PassBuffer.{h,cpp}`, `source/Diag.{h,cpp}` and `source/GLState.h` (radar's, which
is vectrix's, from `resolume-scopes`), by way of langton.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Taken from vcpkg on Windows (`vcpkg.json`); macOS uses the system OpenGL framework.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work checked against

No code was taken from these — but they were how I knew I had it right, or where the
numbers came from.

### Light round a Schwarzschild black hole — K. Schwarzschild (1916); J. L. Synge, Mon. Not. R. Astron. Soc. 131 (1966) 463

The orbit equation (du/dφ)² = 1/b² − u² + 2u³, integrated and tabulated in
`source/Geodesic.cpp`; the critical impact parameter √27 GM/c²; and Synge's angular radius of
the shadow for a static observer, asin( √27 √(1 − 2/d) / d ), in `source/Flight.cpp`.
`sliptest --shadow` measures the black disc against it.

### The strong deflection limit — V. Bozza, Phys. Rev. D 66 (2002) 103001

The deflection of a ray just outside the photon sphere, −ln( b/b_c − 1 ) + ln( 216 (7 − 4√3) )
− π, which `sliptest --geodesic` holds the table to near the photon ring.

### The weak deflection series — C. R. Keeton and A. O. Petters, Phys. Rev. D 72 (2005) 104006

4/b + 15π/(4b²) + 128/(3b³), which `sliptest --geodesic` holds the table to far out.

### Thin accretion discs — N. I. Shakura and R. A. Sunyaev, Astron. Astrophys. 24 (1973) 337; I. D. Novikov and K. S. Thorne (1973)

The disc's temperature profile T ∝ ( (1 − √(r_in/r)) / r³ )^(1/4) with its inner edge at the
innermost stable orbit, r = 6, and the redshift of a Keplerian emitter, √(1 − 3/r) / (1 − Ωλ),
in the scene shader. `sliptest --disc` checks the face-on profile against the harness's own
rays.

### Gravitational lensing in the movie Interstellar — O. James, E. von Tunzelmann, P. Franklin and K. S. Thorne, Class. Quantum Grav. 32 (2015) 065001

Where the disc's far side lensed up over the hole became a familiar picture, and the
account of why its Doppler asymmetry was switched off for the film; Doppler here does the
same at 0. No code, data or images from it.

### The colour matching functions — C. Wyman, P.-P. Sloan and P. Shirley, "Simple analytic approximations to the CIE XYZ color matching functions", JCGT 2 (2013) 1

The multi-lobe piecewise Gaussian fit of the CIE 1931 2° observer, in `source/Blackbody.cpp`.

### The Planckian locus — B. Kang, O. Moon, C. Hong, H. Lee, B. Cho and Y. Kim, J. Korean Phys. Soc. 41 (2002) 865

Their cubic spline of the locus, which `sliptest --planck` holds the blackbody table to;
the tolerance is ANSI C78.377's Duv for a lamp.

### The error function — M. Abramowitz and I. A. Stegun, Handbook of Mathematical Functions, 7.1.26

erf to 1.5e-7, for a streak's blurred ends, in `source/Shaders.cpp`.

### PCG — M. E. O'Neill, "PCG: A family of simple fast space-efficient statistically good algorithms for random number generation" (2014)

The output permutation that is every random number in the shaders and the harness: exact
in 32 bits on every GPU.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — the clip's curve into and out of linear light, and the XYZ to linear sRGB matrix, in `source/Shaders.cpp` and `source/Blackbody.cpp`.
- **CIE 1931 2° standard observer** — the colour of every star, the disc and the corona.
- **CIE 1960 UCS and ANSI C78.377** — where `--planck` measures distance from the locus, and how far is too far.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Jumps to hyperspace and warp in the films, and a television wormhole

The stretching stars of a jump to lightspeed, the rubber band and flash of a warp jump, and
a wormhole a British comedy's model unit made from whipped-up cloud footage (the series'
own effects guide says so). Built from a camera and a shutter; nothing is copied from
anyone's footage, software or design.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
