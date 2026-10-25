# AGENTS.md — slipstream

The why. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-slipstream.md`.

Built 2026-10-25 at Allan's request, from a showmode session: "a new resolume plugin
which creates 'hyperspace jump'/'warp speed' type effects, it should be able to both warp
an underlying image, or act as a generator by itself, and should at a minimum cover the
star wars style 'stretching stars', a star trek style warp jump, and a red dwarf style
wormhole", and mid-build, "it would be cool to include 'black hole' style effects too,
maybe with a corona effect and an event horizon".

## The one idea

A camera flying through space with its shutter open. A star at depth z and distance ρ
from the line of flight is at f ρ / z on the screen; the camera moves on while the
shutter is open, so the star draws a streak along a line out of the vanishing point, as
long as the distance flown. The streak's light is computed in closed form
(`kStarVertex`, `kStarFragment`): light per unit depth L / ( ρ² + z² ) / ΔZ, per unit
screen length that times z² / ( f ρ ), which is uniform wherever z ≫ ρ (the inverse square
and the perspective cancel). The field repeats every D along the line of flight, so a
shutter that flies further than D sees the same column more than once; the copies are
counted per fragment (`copiesAt`), and past one pass a shutter the light per unit length
stops depending on speed: hyperspace's steady radial lines. Exposure is normalised to the
shutter (a camera on auto exposure), so Shutter changes the streak and not the light.

Everything is exposure until the composite, which develops it as film: 1 − exp( −E ) per
channel. Bright cores saturate white and keep their colour in their fringes; the clip
comes in through the inverse of the curve, so the Over at rest is the clip to the bit.

The journey is a phase J (0 normal space, 0.5 in transit, 1 back again); each look is a
script over J (`PoseAt`), evaluated at the shutter's opening and its closing so that
anything that moves in the frame (the rubber band, the snap, the whirlpool's twist)
blurs with the stars.

## Decisions

- **Name.** `Redshift` was the obvious one and is Maxon's renderer, which is in every
  motion designer's dock: confusing in the one audience that matters. A slipstream is the
  air dragged along behind something fast, and in more than one space opera a road through
  hyperspace. The effect is `SW Slip Over`, the fleet's "Over" (Langton Over, Ferro Over)
  in the 16 characters FFGL allows; it is also a knitted tank top, which the README admits
  to. `SR01`/`SR02` were free (`SL01`, `SP01`–`04`, `SS01`, `ST01` are taken).
- **No film names in the plugin.** The looks are Hyperspace, Warp, Wormhole and Black
  Hole. The website's rule is not to name the products a thing stands in for, and the
  README describes each film's effect without naming the film. Allan's call to change.
- **Red Dwarf's wormhole is clouds.** The official effects guide for series VI says the
  model unit made the Rimmerworld wormhole (also used in the titles) from whipped-up cloud
  images. So: three concentric cylinders of domain-warped cloud, not a neon tunnel.
- **The black hole is Schwarzschild, not Kerr.** A spinning hole's shadow is lopsided and
  its rays leave their plane; the tables would be 3D. The disc spins (Keplerian), the hole
  does not. The camera is a static observer, which is why the plunge stops at d = 3.05: a
  static observer cannot exist inside the photon sphere, and the fade to black from there
  and the white hole on the far side are **named cheats**.
- **Doppler defaults to 1.** The approaching side of the disc is far brighter (18 times in
  `--disc`'s geometry) and bluer. Interstellar's makers switched that off for the film
  (James, von Tunzelmann, Franklin and Thorne, Class. Quantum Grav. 32 (2015) 065001); the
  control lets an operator do the same.
- **The clip in hyperspace rides ahead at Clip Depth** and never comes nearer: the one
  cheat in the smear. Its light smears inward as a star's does (u uniform in [1, k],
  surface brightness kept), so a point of the clip becomes an evenly lit line and the
  picture keeps its look rather than exploding past the camera.
- **The star box's cross-section is the view at its far end** (from the vanishing point to
  the frame's farthest corner, carried out to D). A side effect: the star field looks the
  same at any Field of View, which matters only to the black hole's sky, the tunnel and the
  clouds.
- **The streaks are honest.** At hyperspeed each line carries L / ( f ρ D ) per unit
  length, two to three hundred times dimmer than the star was at rest. It reads as the
  films only because the brightest stars are far brighter than saturation (the Pareto
  tail), and film saturates. No streak gain was added; Exposure is the operator's.
- **Trig in the GLSL.** The fleet's no-trig rule was for pixel-exact checks. Here atan
  (the angle round the vanishing point), sin and cos (the whirlpool, the black hole's ray
  directions) are needed; every check that reads what they decide runs at two rasters and
  on the software renderer.
- **The sky behind the black hole is per pixel** (a star per cell of a cube face's grid at
  most, a Gaussian widened by what the hole adds to the pixel's footprint, the mean light
  where that is wider than a cell) rather than a cube map: resolution-independent, and a
  star smeared by the hole blurs instead of aliasing. Stars at a cube face's edge can be
  cut at the seam; not seen in renders, not fixed.
- **Generated files:** `source/StoatworksAbout*.h`, `ATTRIBUTIONS.md`, the README's
  attributions block, `.github/ISSUE_TEMPLATE/` and `.github/FUNDING.yml` come from
  stoatworks-backend's sync scripts since the project was registered (2026-10-25).

## The traps

Ordered by how much time they cost.

**A transit that ends on a frame's boundary must still end.** Leaving the rest at J = 0.5,
the step taken is what is left of the frame, and when the transit ran out on the frame's
own boundary that is rounding residue, under half an ulp of 0.5: J rounded straight back
onto the rest with the timer already reset, and the transit started again, for ever.
Transit 2 s at 60 fps did it; the first `--journey` passed because its timings missed the
boundary, and only the demo film showed it (the Warp segment never left the previous
look's transit). `Journey::Advance` now steps J off the rest by at least one ulp, and
`--journey` runs every whole-frame Transit Time from 0.2 to 5 s at 60, 50 and 30 fps with a
host-style clock (each dt a difference of two readings); with the fix undone 255 of 675
never come back (`tools/mutate.sh`'s fourteenth mutant).

**D and field units.** Speeds and Clip Depth are in D (the field's period, 4 units);
distances are in units. Twice something mixed them: the camera flew a quarter as far as
it should (seen as streaks too short in the first transit render), and the hyperspace
smear's k was 1 + ΔZ / depth with depth in D (`--smear` found the light a third short of
A / k). Convert at the one place a D becomes a distance.

**A box narrower than the view draws its own edge.** The first render had a bright square
at the vanishing point: the far end of a 2 × 2 box seen from 4 deep. Fixed by making the
box's cross-section the view at its far end.

**Binning pixel centres along a diagonal counts them unevenly.** `--streak` first failed by
20% in a ripple every few bins; the streak was right and the measurement was not. Each
pixel's light is now shared among the bins its square covers (a unit square projected on a
line is a trapezoid, `squareShare`).

**A smeared picture is not a smeared star.** The first `--smear` expected a point of the
clip to become a 1/r line holding A ln k / ( k − 1 ). Worked through, the u-uniform average
gives an evenly lit line holding A / k: the picture keeps its surface brightness and was
smaller while it was further off. The harness was wrong, not the shader.

**A mip level as coarse as the taps' spacing aliases their sum.** At level log2( spacing )
a 24-tap smear's total moved by up to 13% with the point's position. Half a level coarser
(a blur of 0.57 spacings, 32 taps) puts the aliasing under 1% (2 exp( −2π² 0.57² )) and
removes the banding.

**Fading the clip by scaling its exposure leaves its highlights at full brightness.** E
times a quarter, developed, is most of white for anything that was white. `fadeLight()`
scales the developed light and goes back into exposure.

**A float clock's errors cancel over a run whose ends are exact in float.** The first
`--clock` compared the distance after 120 frames from 499,000 s to 499,002 s, both exact
floats, so the float clock's stalls and double steps summed to the right answer and the
negative control passed. It checks every frame's step now, which is what an audience
would see.

**MSVC refuses a string literal over about 16 KB** (C2026). The scene shader is 21 KB:
two adjacent raw strings, which `glslc.sh` joins.

**GLSL's smoothstep is undefined with edge0 ≥ edge1.** Two fades were written that way and
worked on Apple's driver. Every fade down is `1 - smoothstep( low, high, x )`.

**A ray traced in fixed steps has to land on the angle it is asked about.** `--disc`'s
face-on check first found no radii to test: its RK4 never visited α = π/2. The step is now
π/2 / 16000 and the state after the last step is the one used.

**`--list` needs two spaces after a 16-character name.** The sweep splits columns on two
spaces; "Source on GitHub" is sixteen.

**The Doppler side is easy to misread by eye.** In the first black hole render the left of
the disc looked the brighter, and the geometry said the right approaches. `--disc` reads
the halves and the velocity: the right is 18 times the brighter. Trust the check.

## Would this hold on another rasteriser, at another raster?

Every GPU check runs at 1280×720 and 320×180; all but `--copies` and `--disc` run again on
Apple's software renderer at 320×180.

- `--streak`, `--copies`: bins of 2σ (4σ) px, the light measured over a strip 5σ + 1 px
  each side; σ ≥ 0.6 px, where a sampled Gaussian sums to 1 within 0.2%. Tolerance 2%
  (half float 0.05%, the sampled Gaussian, light leaking between bins where the level
  changes). Raster-independent by construction.
- `--energy`: the whole frame's light, 0.5% (the sampled Gaussian and half float).
- `--identity`: 8-bit equality; holds because the round trip's error (two exp/log in
  float, half float storage) is under a hundredth of a level.
- `--smear`: 3% on the total, 5% on the evenness (32 taps through a box-filtered mip
  pyramid; trilinear and bilinear weights vary by driver, which is why it is 3% and not
  0.3%).
- `--clock`: CPU only, 1e-6.
- `--shadow`: 0.5 px on a radius from the area of a pixelated disc.
- `--einstein`: 0.5 px on a ring's centroid; the tables are read with texelFetch, so the
  filter unit's 8-bit weights never enter.
- `--disc`: mirror symmetry to 1/1024 of the brightest pixel (half float); the approach
  side by a factor of 1.3 at least (it is 18); the face-on profile to 3%.
- `--ring`: 4% (the shader's 32-point midpoint rule against a 1e-3 rad RK4).
- `--seam`: a ratio of two means of the same texture, three to one.
- `--resize`: bit equality between two runs in one context; would not hold across
  contexts on Apple's software renderer (honeydew's trap), and is not asked to.

## Mutation testing

`tools/mutate.sh`: 14 one-character mutants of the shipped GLSL and C++, each built
arm64-only in a copy of the tree with only its named check run; all 14 caught (2026-10-25):
the star's flux (`--streak`), one copy too many (`--copies`), the clip's exposure sign
(`--identity`), the smear's direction (`--smear`), capture above b_c (`--shadow`), the
photon's angular momentum sign and the emitter's time dilation (`--disc`), the corona's g⁴
(`--ring`), the table's row for an escaping b (`--einstein`), the camera's trapezoids
(`--clock`), entry's rate and the step off transit's rest (`--journey`), the orbit equation's 3u² (`--geodesic`), ȳ's peak
(`--planck`).

## What is verified and what is assumed

Verified, on this Mac (M4 Max, macOS 26) and its software renderer: everything in the
README's Status table. Assumed, and not checked by anything:

- that Resolume loads it (oxbow does);
- that Windows builds it (the workflow is tinsel's lineage and has never run);
- that the films look like this. The looks were judged by eye on harness renders, not
  against footage.
- Wyman et al.'s CIE fit is half the tabulated x̄ at 700 nm; 2000 K comes out 0.0031 from
  the locus in uv. Close enough for stars and a disc, not for a lamp.

## Not done, and open questions

- Never loaded into Resolume; never built for Windows; no GPU but this Mac's.
- No beat sync. A jump on the beat is a Jump press mapped to a beat in Resolume; a Sync
  option (Entry and Exit in beats from the host tempo) is the obvious next control.
- No audio input.
- Kerr: a spinning hole (the shadow's flat side, frame dragging) is the obvious upgrade to
  the black hole, at the cost of 3D tables.
- The plunge and the white hole are cheats; nothing below d = 3.05 is physics.
- Stars at a cube face's edge in the black hole's sky can be cut at the seam.
- No OpenFX port, no browser demo, no user guide, no presets, no video.
