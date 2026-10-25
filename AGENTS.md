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
comes in through the inverse of the curve, so the Over at rest is the clip to the bit
(with Stars at 0: otherwise the stars are laid over it).

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
  does not. The camera is a static observer, held outside the photon sphere: the tables
  sort a ray into escape or capture by its b alone, which is true only for a camera outside
  r = 3 (inside it an outward ray below b_c escapes and one above it falls back). Static
  observers exist anywhere outside the horizon; it is the tables that stop the plunge at
  d = 3.05, and the fade to black from there and the white hole on the far side are
  **named cheats**.
- **Doppler defaults to 1.** The approaching side of the disc is far brighter (18 times in
  `--disc`'s geometry) and bluer. Interstellar's makers switched that off for the film
  (James, von Tunzelmann, Franklin and Thorne, Class. Quantum Grav. 32 (2015) 065001); the
  control lets an operator do the same.
- **The clip in hyperspace rides ahead at Clip Depth** and never comes nearer: the one
  cheat in the smear. Its light smears inward as a star's does (u uniform in [1, k],
  surface brightness kept), so a point of the clip becomes an evenly lit line and the
  picture keeps its look rather than exploding past the camera. It cruises with the
  camera: k counts only the run beyond Cruise Speed, so the clip at rest is sharp at any
  Cruise Speed and smears only in a jump.
- **The Wormhole's streaks are straight chords.** A star's streak is drawn from its
  shutter-open end to its shutter-close end, each turned by the whirlpool at its own
  radius. Where the two turns differ by more than a quarter of a radian the chord is
  nowhere near the star's spiral (it was a straight line across the frame on the way out),
  so the star fades, gone by three quarters: the Wormhole's one cheat with the stars.
  Drawing the spiral would mean covering an annulus a star.
- **The star box's cross-section is the view at its far end** (from the vanishing point to
  the frame's farthest corner, carried out to D). A side effect: the star field fills any
  Field of View, but a wider view spreads the same stars through a wider box, further off
  the line of flight, so it is dimmer (the mean pixel at rest 0.38 at 120° against 0.65 at
  20°). Field of View also sets the black hole's held distance, since Hole Size is a share
  of the frame: wider, and the camera holds nearer the hole and the disc fills more.
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
  star smeared by the hole blurs instead of aliasing. A pixel reads cells out to five sigmas
  in the face's smallest cells (a cell's angle shrinks by up to 1 + s² + t² towards the
  edges), and within five sigmas of a seam the face across it too: each star belongs to one
  face, so nothing is counted twice and nothing is cut. Before that, at Field of View 120°,
  stars were cut off along the seams and the brightest came out square; the fix was judged
  by eye on a render at 120°, and no check measures it.
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
never come back (a mutant in `tools/mutate.sh`).

**So must a cruise.** The same trap at the other rest: Loop's cruise running out exactly
on a frame's boundary left J on 0 with its timer reset, so Cruise Time 2 s or 20 s never
jumped at all, at any frame rate. The guide's author found it sweeping all 1001 slider
positions; `--journey` now does the same at 60, 50 and 30 fps, and leaving 0 steps J off
it by an ulp as leaving 0.5 does.

**A fader is circular.** J's 1 is its 0, and under Drive Fader a frame used to fly from
the last Journey to the new one literally: a looping ramp's wrap from 0.99 to 0 flew the
whole trip backwards in one frame, both flashes and all, every time round. A jump back of
more than half a trip now counts as going round. Forward stays literal: the first version
took the short way round both ways, which sent Journey set straight from 0 to 0.76 backwards
through the exit, missing the white-out, and the sweep found Flash dead. The second version
measured each move from J, which holds 1 as 0: a fader held at the top was a whole trip
forward every frame, every flash on every frame (the video's maker found it). Each move is
from the last Journey as it came now.

**A table read linearly must hold something that is linear between its rows.** The end
table held u_end = 1/r_min, which falls as 1/b across rows evenly spaced in ln( b − b_c ):
read linearly between them it was 3e-4 out, and the Over's clip sat up to half a pixel out
of place while the hole was far off. It holds b u_end now, which runs smoothly to 1. Before
that the table also stopped at b = 22,000 while the hole comes in from 100,000: every ray
past the last row read that row's u_end, and the first half second of every approach was
a black frame streaked with radial lines. The guide's author and the video's both found it
on film; `--shadow` and `--einstein` only ever looked at the held hole. `--approach` looks
at the approach.

**A mip level from an angle is never under a pixel.** The Over's clip behind the black
hole was read at a level from the ray's angular footprint, which turned into pixels is
never less than one and grows towards the corners: the whole clip was a little soft
whenever there was a hole. It is the footprint on the clip, in the clip's pixels, now
(1 for an unbent ray, the clip at its own resolution).

**So is a smear's floor.** The hyperspace smear's level was `log2( max( spacing, 1 ) ) +
0.5`, never under half a step: every smear blurred the clip by a pixel, however short. With
the default Cruise Speed smearing the clip at rest, the Over at its defaults was soft from
the first frame. The floor is 0 now, and cruising does not smear the clip. A sharpness
measure caught it where equality could not: a smear a hundredth of a pixel long moves a
hard edge by levels, since the clip is averaged in exposure. Summed slopes did not see the
blur either (a blur leaves an edge's total rise alone); summed squared slopes do.

**A flash point crossed at the end of a frame shows the pose at its close.** Out of the
black hole the blackout ends and the white hole flashes at J 0.8. A frame whose shutter
crossed 0.8 at its very end drew the pose there (plain space, the blackout over) with a
flash that had had no time to expose: one frame of plain space between black and white.
That frame is now black for the share of its open shutter before the crossing.

**Exposure 8 is white until its last few frames.** The Wormhole's end light went from 8
to 0 in one frame at the white-out (a cut from white, mean light 1.0 to 0.12, whatever
Flash said). Fading the exposure evenly still left it white until the last eight frames;
it fades so that its developed light falls evenly now, over a fifth of the exit.

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
- `--approach`: a mean of 1 level in 255 where the bending is under a tenth of a pixel.
  GPU only: Apple's software renderer's sin and cos put a ray about a hundredth of a pixel
  out, which on the card's hard edges is three levels on average.
- `--chord`: streaks either gone (under 1e-6 of their light) or whole (to 1e-6), with a
  margin of 0.05 rad round the fade's ends for the shader's float turn.
- `--plunge`: a median ratio of pixel light, 0.1 against an expected 0.03; eight-bit
  rounding of the dark sky is why it is a median over lit pixels.
- `--identity`'s jump case: gradient energy 80% of the clip's (90% and 96% here; the old
  floor gave 40%).
- `--seam`: a ratio of two means of the same texture, three to one.
- `--resize`: bit equality between two runs in one context; would not hold across
  contexts on Apple's software renderer (honeydew's trap), and is not asked to.

## Mutation testing

`tools/mutate.sh`: 23 one-character mutants of the shipped GLSL and C++, each built
arm64-only in a copy of the tree with only its named check run; all 23 caught (2026-10-25):
the star's flux (`--streak`), one copy too many (`--copies`), the clip's exposure sign and
the smear's mip floor (`--identity`), the smear's direction and cruising smearing the clip
(`--smear`), capture above b_c (`--shadow`), the photon's angular momentum sign and the
emitter's time dilation (`--disc`), the corona's g⁴ (`--ring`), the table's row for an
escaping b and b u_end read back wrong (`--einstein`), b u_end stored with the wrong b
and the Over's clip at the coarsest mip (`--approach`), the white hole's frame lit from
its start (`--plunge`), the twisted streaks' fade (`--chord`), the camera's trapezoids
(`--clock`), entry's rate, the steps off both rests and the Fader's wrap
(`--journey`), the orbit equation's 3u² (`--geodesic`), ȳ's peak (`--planck`).

## What is verified and what is assumed

Verified, on this Mac (M4 Max, macOS 26) and its software renderer: everything in the
README's Status table. Assumed, and not checked by anything:

- that Resolume on macOS loads it (oxbow does; on Windows the fleet's Arena gate loads
  both in Arena 7.27.1 on win-lab, under Mesa llvmpipe, and checks their parameters and
  that each control moves the picture);
- that the films look like this. The looks were judged by eye on harness renders, not
  against footage;
- that the sky's seams are gone: judged by eye on a render at Field of View 120°.
- Wyman et al.'s CIE fit is half the tabulated x̄ at 700 nm; 2000 K comes out 0.0031 from
  the locus in uv. Close enough for stars and a disc, not for a lamp.

## The browser demo (2026-10-25)

`demo/` is <https://slipstream-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules (`~/Projects/resolume/specs/DEMO-BRIEF.md`) by a sub-agent of
the release session, from langton's demo. What a reader of it must know:

- **The shaders are the plugin's**, all seven pieces plus kVersion, spliced by
  `demo/tools/check_shaders.py --write` into `demo/shaders.js` and compared character for
  character; kSceneFragment's two adjacent raw strings (MSVC's cap) are joined as the
  compiler joins them, and anything but whitespace between them fails. It also fails on a
  piece it does not list and on a change to the JOIN on either side (`Assemble()`,
  InitGL's four stages -- the star pass assembles kCommon into its vertex stage too --
  and plugin.js's copy). **All four programs compile and link in WebGL2 as spliced**
  (ANGLE on Metal, M4 Max). `LightGain` is set by the plugin and declared by the scene
  shader but read by nothing, so it is inactive in both.
- **The CPU half is a port, held to the C++ to the bit.** `demo/plugin.js`'s PORT block
  (Controls, Flight, Blackbody, Geodesic) and FRAME block (ProcessOpenGL's CPU half,
  written to RETURN each pass's uniforms) run under node in `demo/tools/crosscheck/`:
  `printer.cpp` on the plugin's own four GL-free sources (90,546 lines: every slider
  position, every look's script across J at four hold distances, one past kHoleFar, every
  drive at 60/50/30 fps, the camera, all 675 whole-frame Transit Times of the first trap
  and all 675 whole-frame Cruise Times of the Loop's, both tables), and `uniforms.cpp`
  driving the REAL `SlipstreamPlugin` in a CGL context through `scenario.mjs`'s 9,976
  frames of both plugins (every look, every drive, a jump abandoned halfway, a fader
  scrubbed back through the flash points and wrapped round, the Wormhole's way out, the
  white hole crossed at different points of a frame, raster changes, a stalled, reversed
  and jumping clock), reading every active uniform back with glGetUniform after each
  frame. **Identical**: every line, every frame's CPU doubles, all 957,696 uniform values.
- **Two things had to be taken out for that to be exact, and both are measured instead.**
  V8's Math is not Apple's libm: about one distinct exp, expm1, asin or sin/cos result in
  ten differs in the last bit (pow and log2 never did). `libm_record.c` is interposed
  (dyld `__interpose`, DYLD_INSERT_LIBRARIES) to record every libm call the C++ makes, and
  `replay.mjs` answers the port's Math calls from it; calls clang folded at build time
  (the bisection's first steps in DistanceForShadow, log of constants, sin of the
  reference FOV) fall back to V8 and are counted, and the output is identical anyway. And
  arm64's default `-ffp-contract=on` fuses multiply-adds that JavaScript cannot, so the C++
  is built `-ffp-contract=off` (the x86_64 slice's arithmetic). **On V8's own Math and
  against the native arm64 build** -- what the page actually is against what ships --
  every uniform in the 9,976 frames is still the same float; the CPU doubles differ by up
  to 1e-10 (the hole's distance); 6 of 1,051,648 table floats differ by one ulp from the
  arm64 build's (none from the strict one's).
- **Ported to 2d3b679's pre-release fixes the same day** (the reach of the orbit table
  and b u_end in its ends, the Loop's rest step, the fader's wrap, the clip not smeared by
  cruising, the white hole's crossing frame, Swirl in the spin, EndGlow, ChordLaw and
  EndsLaw): check_shaders.py caught the three changed pieces before anything was
  re-spliced, and the port was identical to the plugin again on its first run. Then the
  Fader's move from the last Journey rather than J (`faderLast`: J holds 1 as 0, so a
  fader held at 1.0 flew a whole trip every frame): the cross-check already held a fader
  at 1.0, failed against the C++ without the fix at the first such frame, and is
  identical against the C++ with it.
- **Mutants**: 23 one-token mutants of the port and frame logic, all caught (2026-10-25,
  on 2d3b679): the first trap's fix undone, the camera's trapezoid, the orbit equation's
  3u², a CIE lobe weight, Hole Size's range, the warp's spring, the sky's mean light, the
  flash's age at mid-shutter, the whirlpool's spin rate and Swirl's share of it, a new
  look's flash, the clock's jump rule, the Over's clip levels, and one for each of
  2d3b679's fixes (the Loop's rest step, the fader's wrap, b u_end, the table's reach,
  cruising smearing the clip, the end light's exposure, the white hole's blackout share,
  the white-out's fade, ChordLaw, the approach from past kHoleFar).
- **The GL half only a reader checks** (the bindings, the additive blend, the buffers),
  so frames were compared ONCE with `sliptest --out` at the same settings: 1280x720, 180
  frames on the f/60 clock, Drive Fader held mid-journey, the page's last frame read
  straight off its canvas, the Over's clip the kit's geometry card handed to the harness
  with `--clip`; against 2d3b679's sliptest. SW Slipstream, all four looks
  mid-journey, the Wormhole's way out (J 0.85: the chord fade, the fading white-out) and the
  Black Hole's approach (J 0.1): 99.95-99.995% of pixels identical, none more than one
  level of 255 out. SW Slip Over, Hyperspace, Warp and the Black Hole's approach: 99.98-
  99.995%, none more than one level. SW Slip Over, Black Hole held: 99.97% identical; 48
  of 921,600 pixels differ by more than one level, 13 by more than four (at most 11), on
  the disc's hard outer edge and its tips, where a float rounding decides disc or clip
  against the bright card. (Before 2d3b679 that was 472 pixels over four levels, most of
  them the clip seen past the hole: it is now read at its own pixels' mip level.)
- **Decided without asking**: both plugins on one page (langton's shape, `?plugin=over`,
  a new instance at the defaults); Stars (FF_TYPE_INTEGER 0-20,000) a dropdown of fifteen
  values including the default 4,000, the slot stored; Jump a toggle released after a
  frame; a Look picker that sets the plugin's own Look control; the Over's clips `scene`
  (default), `grid`, `spot`, `bars`, `ramp`, `detail`, `alpha`; the blackbody table falls
  back to RGBA16F (said under the picture) without OES_texture_float_linear; the
  harness's test hooks not ported, their uniforms sent at the plugin's operating values;
  the clock's unit vote not ported (the page's unit is seconds). Paused, dt is 0, so the
  streaks are points: the plugin's behaviour with a stopped clock, not hidden.
- **Differences, all said on the page**: the CPU half and the frame logic are ports (and
  how each is checked); GLSL ES 3.00 (the star pass's vertex-stage blackbody fetch has
  ES's default lowp sampler, which the kit does not override); no audio (the plugin has
  none); events and integers; the page's clock; the Wormhole's cost under the kit's 0.1 s
  cap; the Over's clips; the tables built at load (~160 ms here).
- **Verified headlessly** (cdpshot's Chrome, `--use-angle=metal`, console and Log events
  collected): no console errors, under the page's own `_headers` CSP too; every look of
  both plugins renders; Exposure moved while paused changes the picture and moving it back
  and forth again is bit-identical; the Look picker, the Plugin switch (Clip controls
  appear, defaults return), Mix 0 (the clip back), Jump released after a frame; with
  EXT_color_buffer_float hidden the page stops and says so, with OES_texture_float_linear
  hidden it says the table is half float.
- **Live** needs a proxied AAAA 100:: DNS record for `slipstream-demo` (the zone's custom
  domains are full; `wrangler.toml` has a ROUTE), and `deploy.yml` needs the repo's
  `CLOUDFLARE_API_TOKEN` secret and `CLOUDFLARE_ACCOUNT_ID` variable. The live page will
  log one console error that is not the page's: the zone's injected challenge script,
  blocked by `script-src 'self'`, as on every `*-demo` host.
- `stoatworks-backend/resolume-demo/sync.sh` does not list slipstream yet: add it, or the
  vendored kit here is one nothing checks.

## Not done, and open questions

- Never loaded into Resolume on macOS; on Windows only by the fleet's gate; no GPU but this
  Mac's and llvmpipe.
- **The Wormhole's clouds boil at the edges in transit.** Near the frame's edges the clouds
  are close and run past about two noise cells a frame at the default Top Speed, more than
  any shutter blurs: consecutive frames there are different pictures (correlation about 0,
  against 0.4 in the middle). A tenth of Top Speed steadies them. Fading the domain warp
  with the run was tried and changed nothing measurable; the cure is noise whose features
  grow with their speed past the camera, not done.
- With Tunnel up and a short shutter or a low Top Speed, the tunnel's noise aliases into
  fine rings and spokes at the vanishing point.
- No beat sync. A jump on the beat is a Jump press mapped to a beat in Resolume; a Sync
  option (Entry and Exit in beats from the host tempo) is the obvious next control.
- No audio input.
- Kerr: a spinning hole (the shadow's flat side, frame dragging) is the obvious upgrade to
  the black hole, at the cost of 3D tables.
- The plunge and the white hole are cheats; nothing below d = 3.05 is physics.
- No OpenFX port, no presets.
