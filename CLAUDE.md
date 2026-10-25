# slipstream

Hyperspace, warp, wormholes and a black hole for Resolume Arena/Avenue, as two FFGL
plugins from one core: a camera flying through space with its shutter open. C++/GLSL,
CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| space, with stars in it | `SR01` | `SW Slipstream` | `Slipstream.bundle`, `com.stoatworks.ffgl.slipstream` | source |
| the clip as the view ahead | `SR02` | `SW Slip Over` | `Slipstream Over.bundle`, `com.stoatworks.ffgl.slipstream.over` | effect |

Read `AGENTS.md` before touching the streak's closed form (the star shaders), the journey
(`Flight.cpp`), the orbit table (`Geodesic.cpp`) or the black hole's rays (the scene
shader's second half).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/sliptest --out /tmp/frame.png` (the source on its defaults, 3 s in)
- The Over: `./build/sliptest --over --out /tmp/over.png` (the night-city card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/sliptest --list` (`--over` for the effect's)
- Set anything by name: `./build/sliptest --set "Look=Black Hole" --set "Hole Size=0.4"`
  (a standard control takes the host's 0..1; an option takes its element's name; anything
  that is neither is refused)
- Hold a moment of the journey: `--set Drive=Fader --set Journey=0.5` (0 normal space,
  0.5 in transit, 1 normal space again). A Fader jump lands in one frame: to see the frames
  on the way, ramp Journey with `--script`.
- Later settings: `./build/sliptest --at 90 "Jump=1"`; start mid-journey: `--phase 0.4`
- Film: `./build/sliptest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/sliptest --over --pipe --size WxH | ffmpeg …`
- The summary line of `--out` gives J, the distance flown, the distance flown while the
  shutter was open (in D), the flash and the black hole's distance.

## Verify
- Everything: `tools/verify.sh` (shader hygiene, `glslc`, the submodule pin, a fresh
  universal build with no warnings, both bundles through lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest`; every GPU check at 1280x720 and 320x180; most of them again
  on Apple's software renderer; `--offline`; `--pipe` for both; the negative controls; 13
  mutants; the sweep; the bench). In a sandboxed Claude session set `TMPDIR` to a writable
  scratch directory, or `glslc.sh` hangs on its heredoc. It loads both bundles in
  `oxbow` from `~/Projects/resolume/oxbow/build/oxbow` or `OXBOW=...`, and skips that step
  (saying so) when there is none: build oxbow out of tree (`cmake -S
  ~/Projects/resolume/oxbow -B <scratch>`) rather than in its shared checkout.
- **No GL** (`--offline` runs these and their negatives): `--journey` (each Drive's timings,
  J only forward), `--geodesic` (the orbit table against weak field, Bozza's strong-deflection
  limit and a quadrature of the first integral), `--planck` (the blackbody colours against
  Kang et al.'s Planckian locus), `--names`, `--cues`.
- **The streaks**: `--streak` (one star against a simulated open shutter, light per unit
  length and in all), `--energy` (a star's whole light at rest and at three speeds),
  `--copies` (past one pass of the field a shutter, against the simulated periodic field).
- **The Over**: `--identity` (at rest, every look, the clip back to the bit), `--smear` (a
  point of the clip under hyperspace's smear: the inward segment, evenly lit, A / k).
- **The black hole**: `--shadow` (its size against Synge's closed form), `--einstein` (a dot
  behind it, the ring, against the harness's own RK4 ray), `--disc` (Doppler 0 symmetric,
  the approaching side brighter and bluer, the face-on profile against the harness's own
  rays and sqrt( 1 - 3/r )), `--ring` (the corona along the harness's own rays; the brightest
  ring on the shadow's edge).
- **The rest**: `--clock`, `--seam`, `--resize`, `--state`.
- The checks can fail: `./build/sliptest --negative` (17 wrong models), `tools/mutate.sh`
  (14 mutants).
- No dead controls: `python3 tools/sweep.py` (both plugins).
- Render cost: `./build/sliptest --bench`.
- `SLIPTEST_RENDERER=software` runs any of it on Apple's software renderer.
- `SLIPTEST_TRACE=1` with `--streak`, `--copies`, `--smear` or `--disc` prints the bins.

## Notes
- **Light is exposure until the composite.** Everything adds in exposure (half float);
  the composite develops it as film, 1 - exp( -E ) per channel. The clip comes in through
  the inverse (`kIntakeFragment`), so the Over at rest is the clip to the bit. Fade the
  clip with `fadeLight()` (in light), never by scaling its exposure.
- **Distances are in field units; speeds and Clip Depth are in D = 4 of them.** Two bugs
  so far came from mixing them. `Flight.cpp` multiplies by `kDepth` at the one place it
  integrates; anything new that turns a D into a distance does the same.
- **The camera's distance never reaches the GPU whole:** `PhaseZ()` is it mod D (stars),
  `LongZ()` mod 256 units (tunnel and clouds, whose noise lattices wrap there).
- **J only moves forward** except under Drive Fader. A jump abandoned halfway carries on
  through and out.
- **The black hole's tables are built once** (`geodesic::Get()`, about a tenth of a second,
  on first use) and read by hand with `texelFetch` (`tableAt`), so their interpolation is
  float and not the filter unit's fixed point.
- **GLSL**: reserved words (`patch sample input output filter common active half layout
  flat packed`) and `noise1`–`noise4` must not be names; `verify.sh` greps. `smoothstep` is
  undefined with edge0 >= edge1: every fade down is `1 - smoothstep( low, high, x )`.
  `kSceneFragment` is two adjacent raw strings (MSVC's literal cap); `glslc.sh` joins them.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element
  index; Jump acts on the rising edge.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `slipstream_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide yet)
  and `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend,
  `sync-about.py` and `sync-attributions.py` generate them; edit them there then.
- Local only: `~/Projects/resolume/slipstream` (built in `~/dev/slipstream` and moved), no
  remote, nothing released, not on the website. It is a checkout under `~/Projects`, so
  changes go through a worktree; with no remote, branch it from local `main` and merge back.

## Not done yet
- Never loaded into Resolume on either platform. Never built for Windows. No GPU but this
  Mac's has run it.
- No GitHub repo, no release, no website page, no user guide, no browser demo, no OpenFX
  port, no video, no presets. The workflows in `.github/` have never run.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/slipstream/slipstream.YYYY-MM-DD.log
    %LOCALAPPDATA%\slipstream\logs\slipstream.YYYY-MM-DD.log

`SLIPSTREAM_LOG_DIR` overrides the folder.
