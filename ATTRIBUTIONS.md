# Attributions

Slipstream is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Source-plus-Over shape, parameter table, clock, harness and tooling — Stoatworks langton

<https://github.com/stoatworks-labs/langton>  
Licence: MIT  
Copyright: Stoatworks Labs

One core registered as a source and an Over effect (CMakeLists.txt's two bundles from one OBJECT library), HostOrder() with the About block last, the host-clock unit vote, the harness's rig, PNG writer, cue sheets, --pipe/--film, --negative with its Perturb, --state, --names, --cues and --offline; tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh. All adapted.

### Pass buffers, GL state and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp}, source/Diag.{h,cpp} and source/GLState.h (radar's, which is vectrix's, from resolume-scopes), by way of langton.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The escape of photons from gravitationally intense stars — J. L. Synge, Mon. Not. R. Astron. Soc. 131 (1966) 463

The angular radius of a Schwarzschild black hole's shadow for a static observer, asin( sqrt 27 sqrt( 1 - 2/d ) / d ), which sliptest --shadow measures the black disc against; the orbit equation (du/dphi)^2 = 1/b^2 - u^2 + 2u^3 tabulated in source/Geodesic.cpp is Schwarzschild's (1916).

### Gravitational lensing in the strong field limit — V. Bozza, Phys. Rev. D 66 (2002) 103001

The deflection of a ray just outside the photon sphere, which sliptest --geodesic holds the orbit table to near the photon ring.

### Formalism for testing theories of gravity using lensing by compact objects — C. R. Keeton and A. O. Petters, Phys. Rev. D 72 (2005) 104006

The weak-field deflection series 4/b + 15 pi / (4 b^2) + 128 / (3 b^3), which sliptest --geodesic holds the table to far from the hole.

### Black holes in binary systems: observational appearance — N. I. Shakura and R. A. Sunyaev, Astron. Astrophys. 24 (1973) 337

The thin disc's temperature profile, T ~ ( (1 - sqrt( r_in / r )) / r^3 )^(1/4) with its inner edge at the innermost stable orbit, in the scene shader; sliptest --disc checks the face-on profile against the harness's own rays.

### Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar — O. James, E. von Tunzelmann, P. Franklin and K. S. Thorne, Class. Quantum Grav. 32 (2015) 065001

Where the disc's far side lensed up over the hole became a familiar picture, and the account of why its Doppler asymmetry was switched off for the film; the Doppler control does the same at 0. No code, data or images from it.

### Simple analytic approximations to the CIE XYZ color matching functions — C. Wyman, P.-P. Sloan and P. Shirley, JCGT 2 (2013) 1

The multi-lobe piecewise Gaussian fit of the CIE 1931 2-degree observer behind every star's, the disc's and the corona's colour, in source/Blackbody.cpp.

### Design of advanced color temperature control system for HDTV applications — B. Kang, O. Moon, C. Hong, H. Lee, B. Cho and Y. Kim, J. Korean Phys. Soc. 41 (2002) 865

Their cubic spline of the Planckian locus, which sliptest --planck holds the blackbody table to.

### Handbook of Mathematical Functions, 7.1.26 — M. Abramowitz and I. A. Stegun

erf to 1.5e-7, for a streak's blurred ends, in source/Shaders.cpp.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Jumps to hyperspace and warp in the films, and a television wormhole

The stretching stars of a jump to lightspeed, the rubber band and flash of a warp jump, and a wormhole a British comedy's model unit made from whipped-up cloud footage. Built from a camera and a shutter; nothing is copied from anyone's footage, software or design.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — the clip's curve into and out of linear light, and the XYZ to linear sRGB matrix.
- **CIE 1931 2-degree standard observer** — the colour of every star, the disc and the corona.
- **CIE 1960 UCS and ANSI C78.377** — where sliptest --planck measures distance from the Planckian locus, and how far is too far.
- **Melissa E. O'Neill, PCG (2014)** — the integer hash behind every star, cloud and the sky's cells, exact in 32 bits on every GPU.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
