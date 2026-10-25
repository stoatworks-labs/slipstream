/**
 * Slipstream — browser demo.
 *
 * Jumps to hyperspace, warp jumps, wormholes and a black hole. The one idea,
 * from `AGENTS.md`: **a camera flying through space with its shutter open.**
 * Nothing draws a streak. A star sits at some depth and some distance off the
 * line of flight, the camera moves on while the shutter is open, and the star's
 * image runs outward from the vanishing point for as long as the shutter lets
 * it. SW Slipstream (SR01) is space with stars in it; SW Slip Over (SR02) puts
 * the clip in the front window.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the seven
 *   `R"( ... )"` pieces of `source/Shaders.cpp` plus kVersion (kSceneFragment's
 *   two adjacent raw strings joined, as the compiler joins them), spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character, and checks that they are joined as
 *   `Assemble()` and InitGL join them, and `tools/verify.sh` runs it. They run
 *   as the plugin's four passes -- intake (the Over's clip into exposure, a
 *   mipmapped RGBA16F buffer), the stars (one instanced quad a star, additive
 *   into RGBA16F), the scene (RGBA16F) and the composite into the canvas --
 *   with the same textures: the blackbody table (256 x 1 RGBA32F, linear), the
 *   orbit table (512 x 512 RGBA32F) and its ends (1 x 512 RGBA32F), both read
 *   with texelFetch.
 *
 *   **The CPU half is a port.** The plugin flies its camera on the CPU, in
 *   double, and builds its colour and orbit tables there; here that is
 *   JavaScript, whose numbers are doubles too. Between the PORT markers below:
 *   Controls.cpp (every mapping), Flight.cpp (the journey, the camera and each
 *   look's script), Blackbody.cpp (the colour table) and Geodesic.cpp (the black
 *   hole's orbit tables, RK4 in double). Between the FRAME markers: the CPU half
 *   of Slipstream.cpp's ProcessOpenGL -- the clock and its jump rule, the drive,
 *   the camera, the shutter, the flash averaged over it, the whirlpool's and the
 *   disc's clocks, and every uniform of every pass -- as a function that RETURNS
 *   the uniforms, so the GL code below only binds and draws. Both blocks are held
 *   to the plugin by demo/tools/crosscheck/ (verify.sh runs it): printer.cpp runs
 *   the PORT block's scenario on the plugin's own Controls/Flight/Blackbody/
 *   Geodesic.cpp, and uniforms.cpp drives the REAL plugin class in a GL context
 *   and reads every active uniform back out of its programs after every frame,
 *   against what the FRAME block returns for the same frames.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page.** One class with a flag in the plugin; the Over
 * declares three more controls (Clip Depth, Clip Through, Mix) after the
 * source's, and both constructors set the same defaults. The Plugin dropdown is
 * a new instance, as it would be in Resolume: the journey starts again and
 * every control goes back to its default. `?plugin=over` opens the effect; Copy
 * link carries it.
 *
 * **Nothing audio.** The plugin has no audio input and no beat sync (to jump
 * on the beat, Resolume maps Jump to a beat), so there is nothing to leave out.
 *
 * **Integers and events.** Stars is FF_TYPE_INTEGER, 0 to 20,000, which the
 * kit has no control for: it is a dropdown of fifteen of its values, the
 * default 4,000 among them (the SLOT is what the page stores; the host's
 * integer is STAR_CHOICES[ slot ]). Jump is FF_TYPE_EVENT: a toggle the page
 * reads on the rising edge, as the plugin does, then puts back to 0, which is
 * what a host does with an event.
 *
 * **The clock.** The kit's: seconds accumulated from frame deltas, capped at a
 * tenth of a second a frame, handed to the port as the host's time already in
 * seconds, so the plugin's seconds-or-milliseconds vote is not ported. The jump
 * rule is the plugin's: a step backwards or over a second is a jump and no time
 * passes across it, so Restart leaves the journey where it was. Paused, no
 * time passes, so nothing moves during the shutter and the streaks are points
 * -- as in the plugin when its host's clock stops. Step is one frame of 1/60 s.
 *
 * **The harness's test hooks are not ported** (the flux, copy, length,
 * round-trip, bend, capture and spin-sign laws, the test star, the seam, the
 * disc's texture switch, the float clock): they exist so `sliptest --negative`
 * has wrong models to catch, and the plugin never sets them in a host. Their
 * uniforms are sent at the values the plugin sends in operation.
 *
 * **The About block is not a parameter here**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';

// ==== BEGIN PORT ===========================================================
// Controls.h/Controls.cpp, Flight.h/Flight.cpp, Blackbody.h/Blackbody.cpp and
// Geodesic.h/Geodesic.cpp, ported. No DOM and no GL between the two markers:
// demo/tools/crosscheck/run.mjs slices this block out by its markers, runs it
// under node and diffs what it prints against the same printout from the
// plugin's own C++ (printer.cpp).

const f32 = Math.fround;
/** std::clamp, std::min and std::max exactly: comparisons, so -0 stays -0. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Controls.h: ParamIds (by what a control is, not by host index), groups,
// names, the two plugins' host orders, and the mappings.
//---------------------------------------------------------------------------

const PT = {
  LOOK: 0, DRIVE: 1, ENGAGE: 2, JUMP: 3, JOURNEY: 4, ENTRY_TIME: 5, TRANSIT_TIME: 6, EXIT_TIME: 7, CRUISE_TIME: 8,
  CRUISE_SPEED: 9, TOP_SPEED: 10, HEADING_X: 11, HEADING_Y: 12, FIELD_OF_VIEW: 13, SHUTTER: 14,
  STARS: 15, STAR_COLOUR: 16, EXPOSURE: 17, FLASH: 18, TINT: 19,
  TUNNEL: 20,
  STRETCH: 21, DISPERSION: 22,
  SWIRL: 23, CLOUDS: 24,
  HOLE_SIZE: 25, DISC: 26, DISC_TILT: 27, DISC_SIZE: 28, DISC_HEAT: 29, DOPPLER: 30, CORONA: 31,
  CLIP_DEPTH: 32, CLIP_THROUGH: 33, MIX: 34,
  ABOUT_TEXT: 35,
};
/** The plugin's params[] up to the About block: what the frame logic reads. */
const kParamSlots = PT.ABOUT_TEXT;

/** HostOrder(): the ids each plugin declares, in host order, About block left off. */
function HostOrder(effect) {
  const order = [];
  for (let id = PT.LOOK; id <= PT.CORONA; id += 1) order.push(id);
  if (effect) order.push(PT.CLIP_DEPTH, PT.CLIP_THROUGH, PT.MIX);
  return order;
}

function GroupOf(id) {
  if (id <= PT.CRUISE_TIME) return 'Journey';
  if (id <= PT.SHUTTER) return 'Flight';
  if (id <= PT.TINT) return 'Light';
  if (id <= PT.TUNNEL) return 'Hyperspace';
  if (id <= PT.DISPERSION) return 'Warp';
  if (id <= PT.CLOUDS) return 'Wormhole';
  if (id <= PT.CORONA) return 'Black Hole';
  if (id <= PT.MIX) return 'Clip';
  return 'About';
}

const NAMES = [
  'Look', 'Drive', 'Engage', 'Jump', 'Journey', 'Entry Time', 'Transit Time', 'Exit Time', 'Cruise Time',
  'Cruise Speed', 'Top Speed', 'Heading X', 'Heading Y', 'Field of View', 'Shutter',
  'Stars', 'Star Colour', 'Exposure', 'Flash', 'Tint',
  'Tunnel',
  'Stretch', 'Dispersion',
  'Swirl', 'Clouds',
  'Hole Size', 'Disc', 'Disc Tilt', 'Disc Size', 'Disc Heat', 'Doppler', 'Corona',
  'Clip Depth', 'Clip Through', 'Mix',
];
const NameOf = (id) => NAMES[id] ?? '?';

const Look = { Hyperspace: 0, Warp: 1, Wormhole: 2, BlackHole: 3, Count: 4 };
const LOOK_NAMES = ['Hyperspace', 'Warp', 'Wormhole', 'Black Hole'];
const Drive = { Hold: 0, Trip: 1, Loop: 2, Fader: 3, Count: 4 };
const DRIVE_NAMES = ['Hold', 'Trip', 'Loop', 'Fader'];

const OptionIndex = (value, count) => clamp(lround(value), 0, count - 1);

const clamp01 = (v) => clamp(v, 0.0, 1.0);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = min2(low, high);
  const hi = max2(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
const kSecondsLow = 0.2;
const kSecondsHigh = 20.0;
const kCruiseLow = 0.005;
const kCruiseHigh = 2.0;
const kTopLow = 0.1;
const kTopHigh = 10.0;
const kHeatLow = 2000.0;
const kHeatHigh = 40000.0;
const kClipDepthLow = 0.02;
const kClipDepthHigh = 2.0;

/** Entry, Transit, Exit and Cruise Time: 0.2 to 20 seconds, geometrically. */
const SecondsFromParam = (v) => geometric(v, kSecondsLow, kSecondsHigh);
const ParamFromSeconds = (s) => inverseGeometric(s, kSecondsLow, kSecondsHigh);
/** Cruise Speed in D per second: exactly 0 at the bottom, then 0.005 to 2, geometrically. */
const CruiseFromParam = (v) => (v <= 0.0 ? 0.0 : geometric(v, kCruiseLow, kCruiseHigh));
const ParamFromCruise = (d) => (d <= 0.0 ? 0.0 : inverseGeometric(d, kCruiseLow, kCruiseHigh));
/** Top Speed: a multiplier on the look's own transit speed, 0.1 to 10. */
const TopFromParam = (v) => geometric(v, kTopLow, kTopHigh);
const ParamFromTop = (m) => inverseGeometric(m, kTopLow, kTopHigh);
/** Heading X and Y: -1 (left, bottom edge) to +1 (right, top edge) of the half-size. */
const HeadingFromParam = (v) => 2.0 * clamp01(v) - 1.0;
/** The vertical field of view, 20 to 120 degrees. */
const FieldOfViewFromParam = (v) => 20.0 + 100.0 * clamp01(v);
const ParamFromFieldOfView = (deg) => f32(clamp((deg - 20.0) / 100.0, 0.0, 1.0));
/** The shutter's opening as a fraction of the frame. */
const ShutterFromParam = (v) => clamp01(v);
const kMaxStars = 20000;
/** Exposure as a linear multiplier: -4 to +4 EV, 0 EV at 0.5. std::exp2: JavaScript has none. */
const ExposureFromParam = (v) => Math.pow(2.0, 8.0 * clamp01(v) - 4.0);
const ParamFromExposure = (ev) => f32(clamp((ev + 4.0) / 8.0, 0.0, 1.0));
const StretchFromParam = (v) => 0.95 * clamp01(v);
const SwirlTurnsFromParam = (v) => 6.0 * clamp01(v);
const HoleSizeFromParam = (v) => 0.05 + 0.85 * clamp01(v);
const DiscTiltFromParam = (v) => 90.0 * clamp01(v);
const DiscOuterFromParam = (v) => 8.0 + 32.0 * clamp01(v);
const DiscHeatFromParam = (v) => geometric(v, kHeatLow, kHeatHigh);
const ParamFromDiscHeat = (k) => inverseGeometric(k, kHeatLow, kHeatHigh);
const ClipDepthFromParam = (v) => geometric(v, kClipDepthLow, kClipDepthHigh);
const ParamFromClipDepth = (d) => inverseGeometric(d, kClipDepthLow, kClipDepthHigh);

//---------------------------------------------------------------------------
// Flight.h / Flight.cpp: the journey, the camera and each look's script.
//---------------------------------------------------------------------------

/** The star field's period along the line of flight; every speed is in D per second. */
const kDepth = 4.0;
const kNear = 0.02;
const kLongPeriod = 64.0 * kDepth;

function smoothstep(a, b, x) {
  const t = clamp01((x - a) / (b - a));
  return t * t * (3.0 - 2.0 * t);
}
/** An exponential ramp from 0 (s = 0) to 1 (s = 1): slow, then a rush. */
function expRamp(s, k) {
  s = clamp01(s);
  return Math.expm1(k * s) / Math.expm1(k);
}
function halfOf(J) {
  const j = J - Math.floor(J);
  if (j < 0.5) return { exiting: false, t: 2.0 * j };
  return { exiting: true, t: 2.0 * j - 1.0 };
}
/** Where a black hole comes in from, in GM/c^2 (Flight.h). */
const kHoleFar = 1.0e5;
const kPlunge = 3.05;

function TransitSpeed(look) {
  switch (look) {
    case Look.Hyperspace: return 180.0;
    case Look.Warp: return 40.0;
    case Look.Wormhole: return 30.0;
    case Look.BlackHole: return 0.0;
    default: return 0.0;
  }
}

function newPose() {
  return { speed: 0.0, tunnel: 0.0, pull: 0.0, clipScale: 1.0, twist: 0.0, mouth: 0.0, endLight: 0.0, endGlow: 0.0, holeD: 0.0, disc: 0.0, blackout: 0.0, scene: 1.0 };
}

/** The look's script at phase J (any real; read mod 1). */
function PoseAt(look, J, settings) {
  const pose = newPose();
  const h = halfOf(J);
  switch (look) {
    case Look.Hyperspace:
      if (!h.exiting) {
        pose.speed = expRamp((h.t - 0.1) / 0.7, 7.0);
        pose.tunnel = smoothstep(0.75, 1.0, h.t);
      } else {
        pose.tunnel = 1.0 - smoothstep(0.0, 0.15, h.t);
        pose.speed = h.t < 0.15 ? 1.0 : expRamp(1.0 - (h.t - 0.15) / 0.4, 7.0);
      }
      break;

    case Look.Warp:
      if (!h.exiting) {
        if (h.t < 0.65) {
          const s = h.t / 0.65;
          pose.pull = s * s * s;
        }
        pose.speed = expRamp((h.t - 0.65) / 0.05, 5.0);
        pose.clipScale = 1.0 - smoothstep(0.65, 0.69, h.t);
      } else if (h.t < 0.3) {
        pose.speed = 1.0;
        pose.clipScale = 0.0;
      } else {
        const s = (h.t - 0.3) / 0.7;
        const decay = Math.exp(-6.0 * s);
        pose.speed = 1.0 - smoothstep(0.3, 0.33, h.t);
        pose.clipScale = 1.0 - decay * Math.cos(9.0 * s);
        pose.pull = -0.35 * decay * Math.sin(9.0 * s);
      }
      break;

    case Look.Wormhole:
      if (!h.exiting) {
        pose.twist = smoothstep(0.0, 0.6, h.t);
        pose.mouth = 2.0 * Math.pow(smoothstep(0.15, 0.65, h.t), 1.5);
        pose.speed = expRamp((h.t - 0.3) / 0.5, 4.0);
      } else if (h.t < 0.5) {
        pose.twist = 1.0;
        pose.mouth = 2.0;
        pose.speed = 1.0;
        const grow = smoothstep(0.15, 0.5, h.t);
        pose.endLight = 2.0 * grow * grow * grow;
        pose.endGlow = 1.0;
      } else {
        // Through: the white-out flashes at 0.5 and the scene unwinds out of
        // it as it fades (Flash 0 included: no cut from white).
        pose.twist = 1.0 - smoothstep(0.5, 1.0, h.t);
        pose.speed = 1.0 - smoothstep(0.5, 0.8, h.t);
        pose.endLight = 2.0;
        pose.endGlow = 1.0 - smoothstep(0.5, 0.7, h.t);
      }
      break;

    case Look.BlackHole: {
      const hold = max2(settings.holeHold, kPlunge + 0.1);
      if (!h.exiting) {
        if (J - Math.floor(J) > 0.0) {
          const s = smoothstep(0.0, 1.0, h.t);
          const from = max2(kHoleFar, hold);
          pose.holeD = Math.exp(Math.log(from) + (Math.log(hold) - Math.log(from)) * s);
          pose.disc = smoothstep(0.3, 0.9, h.t);
        }
      } else if (h.t < 0.6) {
        const s = smoothstep(0.0, 0.6, h.t);
        pose.holeD = kPlunge + Math.exp(Math.log(hold - kPlunge) + (Math.log(0.02) - Math.log(hold - kPlunge)) * s);
        pose.disc = 1.0;
        pose.blackout = smoothstep(0.42, 0.6, h.t);
      }
      break;
    }

    default: break;
  }
  return pose;
}

const FLASH_POINTS = [[0.4, 0.575], [0.325, 0.65], [0.75], [0.8]];
const FlashPoints = (look) => FLASH_POINTS[look] ?? [];
function FlashDecay(look) {
  switch (look) {
    case Look.Hyperspace: return 0.12;
    case Look.Warp: return 0.25;
    case Look.Wormhole: return 0.6;
    case Look.BlackHole: return 0.8;
    default: return 0.2;
  }
}

/** Where a camera at distance d sees the shadow's edge, d >= 3. */
function ShadowAngle(d) {
  d = max2(d, 3.0);
  return Math.asin(min2(1.0, Math.sqrt(27.0) * Math.sqrt(1.0 - 2.0 / d) / d));
}
/** The distance at which the shadow's angular radius is `angle`, by bisection in log d. */
function DistanceForShadow(angle) {
  let lo = Math.log(3.0);
  let hi = Math.log(1.0e9);
  for (let i = 0; i < 200; i += 1) {
    const mid = 0.5 * (lo + hi);
    if (ShadowAngle(Math.exp(mid)) > angle) lo = mid;
    else hi = mid;
  }
  return Math.exp(0.5 * (lo + hi));
}

/** std::nextafter( 0.5, 1.0 ): doubles in [0.5, 1) are 2^-53 apart. */
const kAfterHalf = 0.5 + 2 ** -53;
/** std::nextafter( 0.0, 1.0 ): the smallest denormal. */
const kAfterZero = Number.MIN_VALUE;

class Journey {
  constructor() {
    this.start = 0.0;
    this.end = 0.0;
    this.phase = 0.0;
    this.timer = 0.0;
    this.tripping = false;
    this.lastFader = false;
  }

  Start() { return this.start; }
  End() { return this.end; }
  Phase() { return this.phase; }

  Reset() {
    this.start = this.end = this.phase = 0.0;
    this.timer = 0.0;
    this.tripping = false;
  }

  SetPhaseForTest(j) {
    this.start = this.end = this.phase = j;
    this.timer = 0.0;
  }

  /** `input`: { drive, engage, jumpPress, fader, entry, transit, exit, cruise }. */
  Advance(dt, input) {
    if (input.drive === Drive.Fader) {
      // Scrubbing: J is the fader, and J is circular (1 is 0). Back further
      // than half a trip has gone round, not back: a looping ramp's wrap from
      // 0.99 to 0 flies a hundredth forward.
      const target = clamp(input.fader, 0.0, 1.0);
      let delta = target - this.phase;
      if (delta < -0.5) delta += 1.0;
      this.start = this.lastFader ? this.end : this.phase;
      this.end = this.start + delta;
      this.phase = target >= 1.0 ? 0.0 : target;
      this.lastFader = true;
      return;
    }
    this.lastFader = false;

    let j = this.Phase();
    this.start = j;
    let moved = 0.0;
    if (input.drive === Drive.Trip && input.jumpPress && j === 0.0) {
      this.tripping = true;
      this.timer = 0.0;
    }
    const rateIn = 0.5 / max2(input.entry, 1.0e-3);
    const rateOut = 0.5 / max2(input.exit, 1.0e-3);

    let remaining = max2(dt, 0.0);
    for (let guard = 0; remaining > 0.0 && guard < 16; guard += 1) {
      if (j === 0.0) {
        let leave = false;
        if (input.drive === Drive.Hold) leave = input.engage;
        else if (input.drive === Drive.Trip) leave = this.tripping;
        else if (input.drive === Drive.Loop) {
          const wait = input.cruise - this.timer;
          if (wait > remaining) {
            this.timer += remaining;
            remaining = 0.0;
          } else {
            remaining -= max2(wait, 0.0);
            leave = true;
          }
        }
        if (!leave) break;
        this.timer = 0.0;
        const step = min2(remaining * rateIn, 0.5);
        j += step;
        moved += step;
        remaining -= step / rateIn;
        if (j >= 0.5) j = 0.5;
        // A cruise that runs out on the frame's own boundary leaves no time to
        // step with (Cruise Time 2 s at 60 fps): off the rest is off it.
        if (j <= 0.0) j = kAfterZero;
      } else if (j < 0.5) {
        const step = min2(remaining * rateIn, 0.5 - j);
        j += step;
        moved += step;
        remaining -= step / rateIn;
        if (0.5 - j <= 1.0e-15) {
          moved += 0.5 - j;
          j = 0.5;
        }
      } else if (j === 0.5) {
        let leave = false;
        if (input.drive === Drive.Hold) leave = !input.engage;
        else if (input.drive === Drive.Trip || input.drive === Drive.Loop) {
          const wait = input.transit - this.timer;
          if (wait > remaining) {
            this.timer += remaining;
            remaining = 0.0;
          } else {
            remaining -= max2(wait, 0.0);
            leave = true;
          }
        }
        if (!leave) break;
        this.timer = 0.0;
        const step = min2(remaining * rateOut, 0.5);
        j += step;
        moved += step;
        remaining -= step / rateOut;
        // Off the rest is off it: a step under half an ulp of 0.5 would round J
        // back onto the rest with its timer already reset (AGENTS.md's first trap).
        if (j <= 0.5) j = kAfterHalf;
        if (j >= 1.0) {
          j = 0.0;
          this.tripping = false;
        }
      } else {
        const step = min2(remaining * rateOut, 1.0 - j);
        j += step;
        moved += step;
        remaining -= step / rateOut;
        if (1.0 - j <= 1.0e-15) {
          moved += 1.0 - j;
          j = 0.0;
          this.tripping = false;
          this.timer = 0.0;
        }
      }
    }
    this.end = this.start + moved;
    this.phase = j;
  }
}

const kSubsteps = 16;

class Camera {
  constructor() {
    this.total = 0.0;
    this.zMod = 0.0;
    this.zLong = 0.0;
    this.samples = new Float64Array(kSubsteps + 1);
    this.speedNow = 0.0;
  }

  /** One frame: J moved from j0 to j1 over dt; speed is cruise + top x the script's share. */
  Advance(look, j0, j1, dt, cruise, top, settings) {
    const transit = top * TransitSpeed(look) * kDepth;
    const speed = new Float64Array(kSubsteps + 1);
    for (let i = 0; i <= kSubsteps; i += 1) {
      const J = j0 + ((j1 - j0) * i) / kSubsteps;
      speed[i] = cruise * kDepth + transit * PoseAt(look, J, settings).speed;
    }
    const along = new Float64Array(kSubsteps + 1);
    const h = max2(dt, 0.0) / kSubsteps;
    for (let i = 1; i <= kSubsteps; i += 1) along[i] = along[i - 1] + 0.5 * h * (speed[i - 1] + speed[i]);
    for (let i = 0; i <= kSubsteps; i += 1) this.samples[i] = along[i] - along[kSubsteps];
    this.total += along[kSubsteps];
    // JavaScript's % is C's fmod: exact, the sign of the dividend.
    this.zMod = (this.zMod + along[kSubsteps]) % kDepth;
    this.zLong = (this.zLong + along[kSubsteps]) % kLongPeriod;
    this.speedNow = speed[kSubsteps];
  }

  /** Distance flown at fraction f of the last frame, relative to its end: <= 0. */
  OffsetAt(f) {
    f = clamp(f, 0.0, 1.0) * kSubsteps;
    const i = min2(Math.trunc(f), kSubsteps - 1);
    const t = f - i;
    return this.samples[i] + (this.samples[i + 1] - this.samples[i]) * t;
  }

  PhaseZ() { return this.zMod; }
  LongZ() { return this.zLong; }
  Speed() { return this.speedNow; }
  Total() { return this.total; }
}

//---------------------------------------------------------------------------
// Blackbody.h / Blackbody.cpp: Planck against Wyman, Sloan and Shirley's CIE
// fit, to linear sRGB at unit luminance, 256 temperatures from 800 to 120000 K.
//---------------------------------------------------------------------------

const kBlackbodyEntries = 256;
const kMinKelvin = 800.0;
const kMaxKelvin = 120000.0;

function lobe(lambda, mu, below, above) {
  const t = (lambda - mu) / (lambda < mu ? below : above);
  return Math.exp(-0.5 * t * t);
}
function cie(lambda) {
  return [
    1.056 * lobe(lambda, 599.8, 37.9, 31.0) + 0.362 * lobe(lambda, 442.0, 16.0, 26.7)
      - 0.065 * lobe(lambda, 501.1, 20.4, 26.2),
    0.821 * lobe(lambda, 568.8, 46.9, 40.5) + 0.286 * lobe(lambda, 530.9, 16.3, 31.1),
    1.217 * lobe(lambda, 437.0, 11.8, 36.0) + 0.681 * lobe(lambda, 459.0, 26.0, 13.8),
  ];
}
function planck(lambdaNm, kelvin) {
  const c2 = 1.438776877e7;
  const l = lambdaNm;
  return 1.0 / (l * l * l * l * l * Math.expm1(c2 / (l * kelvin)));
}
/** XYZ of a blackbody, 360 to 830 nm in 1 nm steps (Simpson). */
function xyzOf(kelvin) {
  let X = 0.0;
  let Y = 0.0;
  let Z = 0.0;
  for (let i = 0; i <= 470; i += 1) {
    const lambda = 360.0 + i;
    const w = (i === 0 || i === 470) ? 1.0 : (i % 2 ? 4.0 : 2.0);
    const [x, y, z] = cie(lambda);
    const p = planck(lambda, kelvin) * w;
    X += p * x;
    Y += p * y;
    Z += p * z;
  }
  return [X, Y, Z];
}
const kelvinAt = (i) => kMinKelvin * Math.pow(kMaxKelvin / kMinKelvin, (i + 0.5) / kBlackbodyEntries);

/** The table as the texture holds it: rgb (Y = 1) and log2( Y / Y(6500 K) ), float. */
function BlackbodyTable() {
  const out = new Float32Array(kBlackbodyEntries * 4);
  const [, Y0] = xyzOf(6500.0);
  for (let i = 0; i < kBlackbodyEntries; i += 1) {
    const [X, Y, Z] = xyzOf(kelvinAt(i));
    const rgb = [3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
      0.0557 * X - 0.2040 * Y + 1.0570 * Z];
    for (let c = 0; c < 3; c += 1) rgb[c] /= Y;
    // Out of gamut (the reddest embers): lift by white until nothing is
    // negative, then put the luminance back to 1.
    const low = Math.min(rgb[0], rgb[1], rgb[2]);
    if (low < 0.0) {
      for (let c = 0; c < 3; c += 1) rgb[c] -= low;
      const lum = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
      for (let c = 0; c < 3; c += 1) rgb[c] /= lum;
    }
    for (let c = 0; c < 3; c += 1) out[i * 4 + c] = rgb[c];
    out[i * 4 + 3] = Math.log2(Y / Y0);
  }
  return out;
}

//---------------------------------------------------------------------------
// Geodesic.h / Geodesic.cpp: light round a Schwarzschild hole, tabulated once
// in double. u = 1/r, u'' = -u + 3u^2, RK4 in phi, one table per branch.
//---------------------------------------------------------------------------

const kCritical = 5.196152422706632;
const kB = 512;
const kT = 512;
const kS = 512;
const kLogMin = -14.0;
/** ln( b - b_c ) at the far end: b ~ 270,000, past every ray from kHoleFar. */
const kEscapeLogMax = 12.5;
const kStep = 2.0e-3;
const kCaptureLogMax = 1.6447;

const accel = (u, newtonian) => (newtonian ? -u : -u + 3.0 * u * u);

/** The orbit from infinity to periapsis (escape) or the horizon (capture): { phi, u, w, n }. */
function trajectory(b, h, newtonian) {
  let cap = 4096;
  let P = new Float64Array(cap);
  let U = new Float64Array(cap);
  let W = new Float64Array(cap);
  let n = 0;
  const push = (phi, u, w) => {
    if (n === cap) {
      cap *= 2;
      const grow = (a) => { const b2 = new Float64Array(cap); b2.set(a); return b2; };
      P = grow(P); U = grow(U); W = grow(W);
    }
    P[n] = phi; U[n] = u; W[n] = w; n += 1;
  };
  let phi = 0.0;
  let u = 0.0;
  let w = 1.0 / b;
  push(phi, u, w);
  for (let guard = 0; guard < 400000; guard += 1) {
    const k1u = w;
    const k1w = accel(u, newtonian);
    const k2u = w + 0.5 * h * k1w;
    const k2w = accel(u + 0.5 * h * k1u, newtonian);
    const k3u = w + 0.5 * h * k2w;
    const k3w = accel(u + 0.5 * h * k2u, newtonian);
    const k4u = w + h * k3w;
    const k4w = accel(u + h * k3u, newtonian);
    const nu = u + h / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
    const nw = w + h / 6.0 * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
    if (nw <= 0.0) {
      const f = w / (w - nw);
      const pphi = phi + f * h;
      const s = f;
      const s2 = s * s;
      const s3 = s2 * s;
      const pu = (2 * s3 - 3 * s2 + 1) * u + (s3 - 2 * s2 + s) * h * w + (-2 * s3 + 3 * s2) * nu
        + (s3 - s2) * h * nw;
      push(pphi, pu, 0.0);
      return { P, U, W, n };
    }
    if (nu >= 0.5) {
      const f = (0.5 - u) / (nu - u);
      push(phi + f * h, 0.5, w + f * (nw - w));
      return { P, U, W, n };
    }
    phi += h;
    u = nu;
    w = nw;
    push(phi, u, w);
  }
  return { P, U, W, n };
}

/** phi where u = target on the (monotone) trajectory. */
function phiAtU(path, target) {
  const { P, U, n } = path;
  if (target <= 0.0) return 0.0;
  if (target >= U[n - 1]) return P[n - 1];
  let lo = 0;
  let hi = n - 1;
  while (hi - lo > 1) {
    const mid = Math.floor((lo + hi) / 2);
    if (U[mid] < target) lo = mid;
    else hi = mid;
  }
  const f = (target - U[lo]) / (U[hi] - U[lo]);
  return P[lo] + f * (P[hi] - P[lo]);
}

/** u at phi on the trajectory, by the cubic Hermite through (u, w). */
function uAtPhi(path, phi) {
  const { P, U, W, n } = path;
  if (phi <= 0.0) return 0.0;
  if (phi >= P[n - 1]) return U[n - 1];
  let lo = 0;
  let hi = n - 1;
  while (hi - lo > 1) {
    const mid = Math.floor((lo + hi) / 2);
    if (P[mid] < phi) lo = mid;
    else hi = mid;
  }
  const h = P[hi] - P[lo];
  if (h <= 0.0) return U[lo];
  const s = (phi - P[lo]) / h;
  const s2 = s * s;
  const s3 = s2 * s;
  return (2 * s3 - 3 * s2 + 1) * U[lo] + (s3 - 2 * s2 + s) * h * W[lo] + (-2 * s3 + 3 * s2) * U[hi] + (s3 - s2) * h * W[hi];
}

function fillRow(b, phiRow, uRow) {
  const path = trajectory(b, kStep, false);
  const uEnd = path.U[path.n - 1];
  const phiEnd = path.P[path.n - 1];
  for (let j = 0; j < kT; j += 1) {
    const t = j / (kT - 1);
    phiRow[j] = phiAtU(path, uEnd * (1.0 - t * t));
  }
  for (let j = 0; j < kS; j += 1) {
    const s = j / (kS - 1);
    uRow[j] = uAtPhi(path, s * phiEnd) / uEnd;
  }
  return { uEnd, phiEnd };
}

const EscapeB = (i) => kCritical + Math.exp(kLogMin + ((kEscapeLogMax - kLogMin) * i) / (kB - 1));
const CaptureB = (i) => kCritical - Math.exp(kLogMin + ((kCaptureLogMax - kLogMin) * i) / (kB - 1));

/**
 * geodesic::Get(): the textures' contents. `orbits` is [ b ][ j ] x RGBA
 * (phi_in and u / u_end for the escape branch, the same for capture), `ends`
 * [ b ] x RGBA (b u_end, phi_end escape; u_end, phi_end capture), both float as
 * the plugin stores them; the doubles are kept for the cross-check.
 */
function GeodesicTables() {
  const orbits = new Float32Array(kB * kT * 4);
  const ends = new Float32Array(kB * 4);
  const uEndEscape = new Float64Array(kB);
  const phiEndEscape = new Float64Array(kB);
  const uEndCapture = new Float64Array(kB);
  const phiEndCapture = new Float64Array(kB);
  const phiRow = new Float64Array(kT);
  const uRow = new Float64Array(kS);
  for (let i = 0; i < kB; i += 1) {
    let r = fillRow(EscapeB(i), phiRow, uRow);
    uEndEscape[i] = r.uEnd;
    phiEndEscape[i] = r.phiEnd;
    for (let j = 0; j < kT; j += 1) orbits[(i * kT + j) * 4 + 0] = phiRow[j];
    for (let j = 0; j < kS; j += 1) orbits[(i * kT + j) * 4 + 1] = uRow[j];
    // b u_end, not u_end: it runs smoothly to 1 where u_end falls as 1/b.
    ends[i * 4 + 0] = EscapeB(i) * r.uEnd;
    ends[i * 4 + 1] = r.phiEnd;

    r = fillRow(CaptureB(i), phiRow, uRow);
    uEndCapture[i] = r.uEnd;
    phiEndCapture[i] = r.phiEnd;
    for (let j = 0; j < kT; j += 1) orbits[(i * kT + j) * 4 + 2] = phiRow[j];
    for (let j = 0; j < kS; j += 1) orbits[(i * kT + j) * 4 + 3] = uRow[j];
    ends[i * 4 + 2] = r.uEnd;
    ends[i * 4 + 3] = r.phiEnd;
  }
  return { orbits, ends, uEndEscape, phiEndEscape, uEndCapture, phiEndCapture };
}

// ==== END PORT =============================================================

// ==== BEGIN FRAME ==========================================================
// Slipstream.cpp's ProcessOpenGL, its CPU half: everything it works out before
// and between its draws, as a function that RETURNS each pass's uniforms (in
// the order and with the types the plugin sets them) instead of setting them.
// No DOM and no GL here either: demo/tools/crosscheck/run-frames.mjs runs this
// block under node, and uniforms.cpp drives the real plugin class through the
// same frames and reads every uniform back out of its programs.

/** Host seconds: a bigger forward step, or any backward one, is a jump. */
const kMaxFrameDelta = 1.0;
const kStarGain = 1.0;
const kSkyDepth2 = 16.0;
const kTunnelGain = 0.5;
const kCloudGain = 1.0;
const kDiscGain = 1.4;
const kCoronaGain = 0.06;
const kDiscRate = 46.0;
const kDiscPeriod = 400.0;
const kSkyCells = 600.0;
const kMeanLuminosity = 36.0;
const kStarSeed = 0x51f15e5d;

/** Tint's colour: a hue wheel from clamped triangles, lifted towards white, unit luminance. */
function tintOf(hue) {
  const offsets = [0.0, 2.0 / 3.0, 1.0 / 3.0];
  const rgb = [0, 0, 0];
  for (let i = 0; i < 3; i += 1) {
    let f = hue + offsets[i];
    f -= Math.floor(f);
    rgb[i] = 0.3 + 0.7 * clamp(Math.abs(f * 6.0 - 3.0) - 1.0, 0.0, 1.0);
  }
  const lum = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
  return rgb.map((c) => f32(c / lum));
}

/** The solid angle of a rectangle seen from its centre's normal, full angles a and b. */
const rectangleSolidAngle = (a, b) => 4.0 * Math.asin(Math.sin(0.5 * a) * Math.sin(0.5 * b));

// A uniform as the plugin sets it: glUniform{1,2,3,4}f (values already float,
// as every call site casts), glUniform1i, or glUniform1ui.
const uf = (name, ...v) => [name, 'f', v.map(f32)];
const ui = (name, v) => [name, 'i', [v | 0]];
const uu = (name, v) => [name, 'u', [v >>> 0]];

/**
 * The constructor's defaults, by ParamId: the same for both plugins (a
 * ParamFrom* call is the plugin's own inverse mapping, cast to float as
 * params[] holds it).
 */
const kDefaults = {
  [PT.LOOK]: Look.Hyperspace, [PT.DRIVE]: Drive.Loop, [PT.ENGAGE]: 0, [PT.JUMP]: 0, [PT.JOURNEY]: 0,
  [PT.ENTRY_TIME]: ParamFromSeconds(2.0), [PT.TRANSIT_TIME]: ParamFromSeconds(3.0),
  [PT.EXIT_TIME]: ParamFromSeconds(1.5), [PT.CRUISE_TIME]: ParamFromSeconds(3.0),
  [PT.CRUISE_SPEED]: ParamFromCruise(0.08), [PT.TOP_SPEED]: ParamFromTop(1.0),
  [PT.HEADING_X]: 0.5, [PT.HEADING_Y]: 0.5, [PT.FIELD_OF_VIEW]: ParamFromFieldOfView(60.0), [PT.SHUTTER]: 0.5,
  [PT.STARS]: 4000, [PT.STAR_COLOUR]: f32(0.6), [PT.EXPOSURE]: ParamFromExposure(0.0), [PT.FLASH]: f32(0.7), [PT.TINT]: f32(0.6),
  [PT.TUNNEL]: f32(0.6), [PT.STRETCH]: f32(0.75), [PT.DISPERSION]: f32(0.4), [PT.SWIRL]: 0.5, [PT.CLOUDS]: 0.75,
  [PT.HOLE_SIZE]: f32(0.3), [PT.DISC]: f32(0.8), [PT.DISC_TILT]: f32(0.93), [PT.DISC_SIZE]: f32(0.4),
  [PT.DISC_HEAT]: ParamFromDiscHeat(4500.0), [PT.DOPPLER]: 1.0, [PT.CORONA]: f32(0.35),
  [PT.CLIP_DEPTH]: ParamFromClipDepth(0.3), [PT.CLIP_THROUGH]: 0.25, [PT.MIX]: 1.0,
};

/** ensureClipBuffer's level count: one, and one more per halving of the larger side. */
function ClipLevelsFor(width, height) {
  let levels = 1;
  for (let size = Math.max(width, height); size > 1; size = Math.trunc(size / 2)) levels += 1;
  return levels;
}

/** One instance: what `SlipstreamPlugin` holds between frames. */
class SlipstreamFrames {
  constructor(isEffect) {
    this.isEffect = isEffect;
    this.journey = new Journey();
    this.camera = new Camera();
    this.jumpHeld = false;
    this.flashAge = 1.0e9;
    this.lastLook = -1;
    this.spinClock = 0.0;
    this.discClock = 0.0;
    this.lastNow = -1.0;
    this.frameDt = 0.0;
    this.shutterZ = 0.0;
    this.focal = 0.0;
    this.flashLight = 0.0;
    this.holeD = 0.0;
    this.discLight = 0.0;
    this.coronaLight = 0.0;
  }

  /**
   * One ProcessOpenGL. `params` is the plugin's params[] by ParamId (a
   * Float32Array, as the plugin's is float), `now` the host's time in seconds,
   * `clipLevels` the Over's clip buffer's mip levels (1 for the source, whose
   * buffer is never allocated). Returns the look, the star count and, per pass,
   * the uniforms it sets (null for a pass that does not run this frame).
   */
  frame(params, width, height, now, clipLevels) {
    const isEffect = this.isEffect;

    //-------------------------------------------------------------------
    // Time: the host's, frame to frame, in double. (UpdateClock's unit vote
    // is not here: this host's unit is seconds, as the harness sets it.)
    //-------------------------------------------------------------------
    let dt = 0.0;
    if (this.lastNow >= 0.0) {
      const stepTime = now - this.lastNow;
      if (stepTime >= 0.0 && stepTime <= kMaxFrameDelta) dt = stepTime;
    }
    this.lastNow = now;
    this.frameDt = dt;

    //-------------------------------------------------------------------
    // The journey and the camera.
    //-------------------------------------------------------------------
    const look = OptionIndex(params[PT.LOOK], Look.Count);
    const jumpDown = params[PT.JUMP] > 0.5;
    const drive = {
      drive: OptionIndex(params[PT.DRIVE], Drive.Count),
      engage: params[PT.ENGAGE] > 0.5,
      jumpPress: jumpDown && !this.jumpHeld,
      fader: clamp(params[PT.JOURNEY], 0.0, 1.0),
      entry: SecondsFromParam(params[PT.ENTRY_TIME]),
      transit: SecondsFromParam(params[PT.TRANSIT_TIME]),
      exit: SecondsFromParam(params[PT.EXIT_TIME]),
      cruise: SecondsFromParam(params[PT.CRUISE_TIME]),
    };
    this.jumpHeld = jumpDown;
    this.journey.Advance(dt, drive);
    const j0 = this.journey.Start();
    const j1 = this.journey.End();

    const fov = (FieldOfViewFromParam(params[PT.FIELD_OF_VIEW]) * kPi) / 180.0;
    const focal = (0.5 * height) / Math.tan(0.5 * fov);
    this.focal = focal;
    const settings = {
      stretch: StretchFromParam(params[PT.STRETCH]),
      holeHold: DistanceForShadow(HoleSizeFromParam(params[PT.HOLE_SIZE]) * 0.5 * fov),
    };
    this.camera.Advance(look, j0, j1, dt, CruiseFromParam(params[PT.CRUISE_SPEED]), TopFromParam(params[PT.TOP_SPEED]), settings);

    const shutter = ShutterFromParam(params[PT.SHUTTER]);
    const shutterZ = max2(0.0, -this.camera.OffsetAt(1.0 - shutter));
    this.shutterZ = shutterZ;
    // The part of that run that is only Cruise Speed: the clip cruises with
    // the camera, so this much of it smears nothing.
    const cruiseZ = CruiseFromParam(params[PT.CRUISE_SPEED]) * kDepth * shutter * max2(dt, 0.0);
    const open = PoseAt(look, j0 + (j1 - j0) * (1.0 - shutter), settings);
    const close = PoseAt(look, j1, settings);
    this.holeD = look === Look.BlackHole ? close.holeD : 0.0;

    //-------------------------------------------------------------------
    // The flash: a crossing of one of the look's flash points this frame,
    // then an exponential decay, averaged over the open shutter.
    //-------------------------------------------------------------------
    let crossAt = -1.0;
    {
      let crossed = false;
      let sinceCross = 0.0;
      for (const f of FlashPoints(look)) {
        let at = -1.0;
        if (j1 > j0) {
          const m = Math.floor(j1 - f);
          if (f + m > j0) at = (f + m - j0) / (j1 - j0);
        } else if (j1 < j0) {
          const m = Math.ceil(j1 - f);
          if (f + m < j0) at = (j0 - (f + m)) / (j0 - j1);
        }
        if (at >= 0.0) {
          crossed = true;
          sinceCross = (1.0 - at) * dt;
          crossAt = at;
        }
      }
      this.flashAge = crossed ? sinceCross : this.flashAge + dt;
      if (look !== this.lastLook) this.flashAge = crossed ? this.flashAge : 1.0e9;
      this.lastLook = look;
      const tau = FlashDecay(look);
      const openTime = shutter * (dt > 0.0 ? dt : 1.0 / 60.0);
      const hi = this.flashAge;
      const lo = max2(this.flashAge - openTime, 0.0);
      let average = 0.0;
      if (hi >= 0.0 && hi < 1.0e8) {
        average = openTime > 1.0e-9 ? (tau * (Math.exp(-lo / tau) - Math.exp(-hi / tau))) / openTime : Math.exp(-hi / tau);
      }
      this.flashLight = params[PT.FLASH] * average;
    }

    // The whirlpool's spin and the disc's clock: phases kept small in double.
    // Swirl sets the clouds' spin as well as the whirlpool's turn; its
    // default, 3 turns, is the rate as it was.
    const spinRate = (0.4 + 1.6 * close.speed) * close.twist * SwirlTurnsFromParam(params[PT.SWIRL]) / 3.0;
    this.spinClock = (this.spinClock + dt * spinRate) % (18.0 * kPi);
    this.discClock = (this.discClock + dt * kDiscRate) % kDiscPeriod;

    const exposure = ExposureFromParam(params[PT.EXPOSURE]);
    const vanish = [f32(0.5 * width * (1.0 + HeadingFromParam(params[PT.HEADING_X]))),
      f32(0.5 * height * (1.0 + HeadingFromParam(params[PT.HEADING_Y])))];
    const sigma = f32(max2(0.6, (0.8 * height) / 1080.0));
    const stretch = f32(settings.stretch);
    const pinch = f32(0.3 * height);
    const swirl = f32(0.25 * height);
    const swirlTurns = SwirlTurnsFromParam(params[PT.SWIRL]) * 2.0 * kPi;
    const twist0 = f32(open.twist * swirlTurns);
    const twist1 = f32(close.twist * swirlTurns);
    const tint = tintOf(params[PT.TINT]);
    const clampF = (id) => clamp(params[id], 0.0, 1.0);

    const passes = { intake: null, stars: null, scene: null, composite: null };

    //-------------------------------------------------------------------
    // Intake (Over): the clip into exposure, and its mipmaps.
    //-------------------------------------------------------------------
    if (isEffect) passes.intake = [ui('ClipIn', 0), uf('RoundTripForTest', 1.0)];

    //-------------------------------------------------------------------
    // Stars: the streaks, additive, into their own exposure.
    //-------------------------------------------------------------------
    const starCount = clamp(lround(params[PT.STARS]), 0, kMaxStars);
    if (look !== Look.BlackHole && starCount > 0) {
      // The farthest corner of the frame from the vanishing point, carried out
      // to the far end of the box. std::hypot of two floats is hypotf.
      const reach = f32(Math.hypot(max2(vanish[0], f32(width - vanish[0])), max2(vanish[1], f32(height - vanish[1]))));
      passes.stars = [
        ui('Blackbody', 0),
        uu('StarSeed', kStarSeed),
        uf('TubeHalf', (reach * (kNear + kDepth)) / focal),
        uf('PhaseZ', this.camera.PhaseZ()),
        uf('ShutterZ', shutterZ),
        uf('Focal', focal),
        uf('Vanish', vanish[0], vanish[1]),
        uf('Raster', width, height),
        uf('Sigma', sigma),
        uf('StarGain', exposure * kStarGain),
        uf('StarColour', clampF(PT.STAR_COLOUR)),
        uf('TestStar', 0.0, 0.0, 0.0, 0.0),
        uf('Pull0', open.pull),
        uf('Pull1', close.pull),
        uf('Twist0', look === Look.Wormhole ? twist0 : 0.0),
        uf('Twist1', look === Look.Wormhole ? twist1 : 0.0),
        uf('Stretch', stretch),
        uf('PinchRadius', pinch),
        uf('SwirlRadius', swirl),
        uf('Dispersion', look === Look.Warp ? clampF(PT.DISPERSION) : 0.0),
        uf('CopyLaw', 1.0),
        uf('ChordLaw', 1.0),
        uf('FluxLaw', 1.0),
        uf('LengthLaw', 1.0),
      ];
    }

    //-------------------------------------------------------------------
    // The scene.
    //-------------------------------------------------------------------
    const through = isEffect ? clampF(PT.CLIP_THROUGH) : 0.0;
    const clipDepth = ClipDepthFromParam(params[PT.CLIP_DEPTH]);
    let clipShare = 1.0;
    if (look === Look.Hyperspace) clipShare = f32(1.0 + (through - 1.0) * close.tunnel);
    else if (look === Look.Warp) clipShare = f32(through * (1.0 - clamp(close.clipScale, 0.0, 1.0)));
    const tilt = (DiscTiltFromParam(params[PT.DISC_TILT]) * kPi) / 180.0;
    this.discLight = clampF(PT.DISC) * close.disc * exposure * kDiscGain;
    this.coronaLight = clampF(PT.CORONA) * close.disc * exposure * kCoronaGain;
    const timeA = this.discClock;
    const timeB = (this.discClock + 0.5 * kDiscPeriod) % kDiscPeriod;
    const aspect = width / height;
    const refFov = (60.0 * kPi) / 180.0;
    const frameSolid = rectangleSolidAngle(2.0 * Math.atan(aspect * Math.tan(0.5 * refFov)), refFov);
    const cellSolid = (2.0 / kSkyCells) * (2.0 / kSkyCells);
    // The sky shows a third as many stars as the box does at rest.
    const probability = clamp(((starCount / 3.0) * cellSolid) / frameSolid, 0.0, 1.0);
    passes.scene = [
      ui('Stars', 0), ui('ClipE', 1), ui('Orbits', 2), ui('Ends', 3),
      uf('EndsLaw', 1.0),
      ui('Blackbody', 4),
      ui('LookId', look),
      ui('IsEffect', isEffect ? 1 : 0),
      uf('Raster', width, height),
      uf('ViewOrigin', 0.0, 0.0),
      uf('Vanish', vanish[0], vanish[1]),
      uf('Focal', focal),
      uf('ClipLevels', clipLevels - 1),
      uf('ClipThrough', through),
      uf('LightGain', exposure),
      uf('TintColour', tint[0], tint[1], tint[2]),
      // Hyperspace: the clip's smear, k = 1 + dZ / Clip Depth with dZ the run
      // beyond cruising, and its share.
      uf('SmearK', 1.0 + max2(shutterZ - cruiseZ, 0.0) / (clipDepth * kDepth)),
      uf('ClipShare', clipShare),
      uf('TunnelLight', clampF(PT.TUNNEL) * close.tunnel * exposure * kTunnelGain),
      uf('LongZ', this.camera.LongZ()),
      uf('ShutterZ', shutterZ),
      // Warp
      uf('Stretch', stretch),
      uf('PinchRadius', pinch),
      uf('SwirlRadius', swirl),
      uf('Pull0', open.pull),
      uf('Pull1', close.pull),
      uf('Scale0', open.clipScale),
      uf('Scale1', close.clipScale),
      // Wormhole
      uf('Twist0', twist0),
      uf('Twist1', twist1),
      uf('MouthRadius', close.mouth * height),
      uf('EndLight', close.endLight * height),
      // The end light's exposure: 8 at full, and as it fades its developed
      // light, 1 - exp( -E ), falls evenly with endGlow.
      uf('EndGlow', -Math.log(1.0 - clamp(close.endGlow, 0.0, 1.0) * (1.0 - Math.exp(-8.0))) * exposure),
      uf('CloudLight', exposure * kCloudGain),
      uf('CloudDensity', clampF(PT.CLOUDS)),
      uf('Spin', this.spinClock),
      // Black Hole
      uf('HoleD', this.holeD),
      uf('DiscNormal', 0.0, Math.sin(tilt), -Math.cos(tilt)),
      uf('DiscInner', 6.0),
      uf('DiscOuter', DiscOuterFromParam(params[PT.DISC_SIZE])),
      uf('DiscKelvin', DiscHeatFromParam(params[PT.DISC_HEAT])),
      uf('DiscLight', this.discLight),
      uf('DopplerAmount', clampF(PT.DOPPLER)),
      uf('CoronaLight', this.coronaLight),
      uf('DiscTimeA', timeA),
      uf('DiscTimeB', timeB),
      // A's weight peaks halfway through its period and is zero where it restarts.
      uf('DiscBlend', Math.abs((2.0 * timeA) / kDiscPeriod - 1.0)),
      uf('SkyCells', kSkyCells),
      uf('SkyProbability', probability),
      uf('SkyGain', (exposure * kStarGain) / (kSkyDepth2 * focal * focal)),
      uf('SkyMean', (probability * kMeanLuminosity) / cellSolid),
      uf('SkySigma', sigma / focal),
      uf('StarColour', clampF(PT.STAR_COLOUR)),
      uu('StarSeed', kStarSeed),
      uf('BendLaw', 1.0),
      uf('CaptureLaw', 1.0),
      uf('SpinSign', 1.0),
      uf('DiscTexture', 1.0),
      uf('SeamForTest', 0.0),
    ];

    //-------------------------------------------------------------------
    // Composite: develop, into the host's framebuffer.
    //-------------------------------------------------------------------
    passes.composite = [
      ui('SceneE', 0), ui('ClipIn', 1),
      ui('IsEffect', isEffect ? 1 : 0),
      uf('MixAmount', isEffect ? clampF(PT.MIX) : 1.0),
      // The frame that comes out of the white hole is black for the part of
      // its open shutter before the crossing, not the plain space of the pose
      // at shutter close.
      uf('Blackout', look === Look.BlackHole && crossAt >= 0.0
        ? max2(close.blackout, open.blackout * clamp((crossAt - (1.0 - shutter)) / shutter, 0.0, 1.0))
        : close.blackout),
      uf('FlashLight', this.flashLight),
      ui('FlashKind', look),
      uf('FlashAge', min2(max2(this.flashAge - 0.5 * shutter * dt, 0.0), 1.0e4)),
      uf('Vanish', vanish[0], vanish[1]),
      uf('Raster', width, height),
      uf('ViewOrigin', 0.0, 0.0),
      uf('TintColour', tint[0], tint[1], tint[2]),
    ];

    return { look, starCount, passes, close };
  }
}

// ==== END FRAME ============================================================

//===========================================================================
// The GL half: the plugin's textures, buffers and draws, with the uniforms
// the FRAME block returned.
//===========================================================================

function floatTexture(gl, width, height, data, internalFormat, filter) {
  const t = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, t);
  gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, width, height, 0, gl.RGBA, gl.FLOAT, data);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return t;
}

/** What the page built once, at InitGL: the tables, and what it cost. */
const tables = { blackbody: null, geodesic: null, ms: 0 };
function buildTables() {
  if (!tables.geodesic) {
    const t0 = performance.now();
    tables.blackbody = BlackbodyTable();
    tables.geodesic = GeodesicTables();
    tables.ms = performance.now() - t0;
  }
  return tables;
}

function createRenderer(gl, quad) {
  // InitGL: the quad's vertex stage is kVersion + kQuadVertex; every other
  // stage is Assemble( piece ) = kVersion + kCommon + piece, the star pass's
  // vertex stage included.
  const quadVertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = (piece) => S.VERSION + S.COMMON + piece;
  const programs = {
    intake: new Program(gl, quadVertex, assemble(S.INTAKE_FRAGMENT), 'intake'),
    stars: new Program(gl, assemble(S.STAR_VERTEX), assemble(S.STAR_FRAGMENT), 'stars'),
    scene: new Program(gl, quadVertex, assemble(S.SCENE_FRAGMENT), 'scene'),
    composite: new Program(gl, quadVertex, assemble(S.COMPOSITE_FRAGMENT), 'composite'),
  };

  // uploadTables(). The blackbody table is filtered (GL_LINEAR) in the plugin,
  // and a linearly filtered RGBA32F texture needs OES_texture_float_linear in
  // WebGL2. Without it the page falls back to RGBA16F and says so.
  const floatLinear = !!gl.getExtension('OES_texture_float_linear');
  const { blackbody, geodesic, ms } = buildTables();
  const blackbodyTexture = floatTexture(gl, kBlackbodyEntries, 1, blackbody, floatLinear ? gl.RGBA32F : gl.RGBA16F, gl.LINEAR);
  const orbitTexture = floatTexture(gl, kT, kB, geodesic.orbits, gl.RGBA32F, gl.NEAREST);
  const endTexture = floatTexture(gl, 1, kB, geodesic.ends, gl.RGBA32F, gl.NEAREST);
  // A sampler bound to texture 0 is "unloadable" to Apple's GL: the source has
  // no clip, so its clip samplers get this.
  const blankClip = floatTexture(gl, 1, 1, new Float32Array(4), gl.RGBA16F, gl.NEAREST);
  // The stars draw from gl_InstanceID and gl_VertexID alone.
  const starArray = gl.createVertexArray();

  const starBuffer = new PassBuffer(gl, { filter: 'nearest' });
  const sceneBuffer = new PassBuffer(gl, { filter: 'nearest' });
  const clipBuffer = new PassBuffer(gl, { filter: 'linear', mip: true });

  let instance = null;
  let instanceVariant = null;
  let stats = null;
  const hostParams = new Float32Array(kParamSlots);

  const apply = (program, uniforms) => {
    for (const [name, kind, v] of uniforms) {
      const loc = program.location(name);
      if (loc === null) {
        program.missing.add(name);
        continue;
      }
      if (kind === 'i') gl.uniform1i(loc, v[0]);
      else if (kind === 'u') gl.uniform1ui(loc, v[0]);
      else if (v.length === 1) gl.uniform1f(loc, v[0]);
      else if (v.length === 2) gl.uniform2f(loc, v[0], v[1]);
      else if (v.length === 3) gl.uniform3f(loc, v[0], v[1], v[2]);
      else gl.uniform4f(loc, v[0], v[1], v[2], v[3]);
    }
  };
  const unbind = (count) => {
    for (let unit = count - 1; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
  };

  /**
   * One ProcessOpenGL. `host` is params[] by ParamId; `input` the clip (the
   * Over), premultiplied, as the kit hands it.
   */
  function processFrame(st, host, input, width, height, now, target = null) {
    const isEffect = st.isEffect;
    const w = isEffect ? input.width : width;
    const h = isEffect ? input.height : height;
    gl.disable(gl.BLEND);

    // Everything allocated before anything is bound.
    starBuffer.ensure(w, h, gl.RGBA16F);
    sceneBuffer.ensure(w, h, gl.RGBA16F);
    // The kit's PassBuffer allocates 1 + floor(log2(max)) levels, the same
    // count. The source never allocates one: its clipLevels stays at 1.
    if (isEffect) clipBuffer.ensure(w, h, gl.RGBA16F);
    const clipLevels = isEffect ? ClipLevelsFor(w, h) : 1;

    const { look, starCount, passes, close } = st.frame(host, w, h, now, clipLevels);

    if (passes.intake) {
      clipBuffer.bind();
      const p = programs.intake.use();
      bindTexture(gl, 0, input.texture);
      apply(p, passes.intake);
      quad.draw();
      unbind(1);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      clipBuffer.generateMipmap();
    }

    starBuffer.bind();
    gl.clearBufferfv(gl.COLOR, 0, [0, 0, 0, 0]);
    if (passes.stars) {
      const p = programs.stars.use();
      bindTexture(gl, 0, blackbodyTexture);
      apply(p, passes.stars);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE);
      gl.bindVertexArray(starArray);
      gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, starCount);
      gl.bindVertexArray(null);
      gl.disable(gl.BLEND);
      unbind(1);
    }

    sceneBuffer.bind();
    {
      const p = programs.scene.use();
      bindTexture(gl, 0, starBuffer.texture);
      bindTexture(gl, 1, isEffect ? clipBuffer.texture : blankClip);
      bindTexture(gl, 2, orbitTexture);
      bindTexture(gl, 3, endTexture);
      bindTexture(gl, 4, blackbodyTexture);
      apply(p, passes.scene);
      quad.draw();
      unbind(5);
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, target);
    gl.viewport(0, 0, width, height);
    {
      const p = programs.composite.use();
      bindTexture(gl, 0, sceneBuffer.texture);
      bindTexture(gl, 1, isEffect ? input.texture : blankClip);
      apply(p, passes.composite);
      quad.draw();
      unbind(2);
    }

    return { look, starCount, close };
  }

  return {
    get stats() { return stats; },
    get programs() { return programs; },
    floatLinear,
    tableMs: ms,
    processFrame,
    newInstance: (isEffect) => new SlipstreamFrames(isEffect),

    render({ input, params, width, height, time, variant }) {
      const isEffect = variant === 'over';
      if (!instance || instanceVariant !== variant) {
        instance = new SlipstreamFrames(isEffect);
        instanceVariant = variant;
      }
      const st = instance;

      // The plugin's params[]: what the host set, as a float. Stars is held as
      // its dropdown slot here; the host's integer is STAR_CHOICES[ slot ].
      for (const p of PARAMS) {
        if (p.slot === undefined) continue;
        hostParams[p.slot] = p.id === 'stars' ? STAR_CHOICES[params.get('stars')] : params.get(p.id);
      }
      const jumpDown = hostParams[PT.JUMP] > 0.5;

      const { look, starCount, close } = processFrame(st, hostParams, input, width, height, time);

      // A host lets go of an event; the kit has none, so the page does.
      if (jumpDown) params.set('jump', 0);

      stats = {
        look,
        starCount,
        J: st.journey.Phase(),
        speed: st.camera.Speed() / kDepth,
        total: st.camera.Total() / kDepth,
        shutter: st.shutterZ / kDepth,
        flash: st.flashLight,
        hole: st.holeD,
        dt: st.frameDt,
        tunnel: close.tunnel,
      };
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in HostOrder()'s order and groups, with
// the plugin's names and defaults. `only` says which plugin declares it.
//===========================================================================

/** Stars' values on the page: FF_TYPE_INTEGER 0..20,000 in the plugin. */
const STAR_CHOICES = [0, 100, 250, 500, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 15000, 20000];

const fix = (n, d = 2) => n.toFixed(d);
const grouped = (n) => Math.round(n).toLocaleString('en-GB');
const sig = (n) => (n >= 1000 ? grouped(n) : n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const signed = (n, d = 2) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;
const V = (v) => f32(v);

/**
 * id, ParamId, FFGL type, display, hint. The defaults are the constructor's,
 * computed by the plugin's own inverse mappings (as floats, as params[] holds
 * them). Hints are from the README and Controls.h.
 */
const SPEC = [
  ['look', PT.LOOK, 'option', null, 'Which journey: Hyperspace (points stretch into streaks, the tunnel), Warp (the rubber band, the snap and the flash), Wormhole (a whirlpool of cloud) or Black Hole (Schwarzschild’s: the shadow, a lensed sky, the disc and its corona).', LOOK_NAMES],
  ['drive', PT.DRIVE, 'option', null, 'How you travel. Hold: Engage on jumps and stays in transit, off comes back out. Trip: each Jump is a round trip. Loop (the default): round trips for ever, Cruise Time between them. Fader: Journey is the trip, 0 normal space, 0.5 in transit, 1 normal space again.', DRIVE_NAMES],
  ['engage', PT.ENGAGE, 'boolean', null, 'Drive Hold: on jumps and stays in transit; off comes back out. A jump you change your mind about halfway carries on through and out rather than running backwards.'],
  ['jump', PT.JUMP, 'event', null, 'Drive Trip: one round trip a press (in, Transit Time there, out). FF_TYPE_EVENT in the plugin; a toggle the page releases after one frame here.'],
  ['journey', PT.JOURNEY, 'standard', (v) => fix(V(v), 3), 'Drive Fader: the trip itself. 0 is normal space, 0.5 is in transit, 1 is normal space again; scrub it either way.'],
  ['entryTime', PT.ENTRY_TIME, 'standard', (v) => `${sig(SecondsFromParam(V(v)))} s`, 'How long the jump in takes: 0.2 to 20 seconds, geometrically.'],
  ['transitTime', PT.TRANSIT_TIME, 'standard', (v) => `${sig(SecondsFromParam(V(v)))} s`, 'How long Trip and Loop stay in transit: 0.2 to 20 seconds.'],
  ['exitTime', PT.EXIT_TIME, 'standard', (v) => `${sig(SecondsFromParam(V(v)))} s`, 'How long dropping out takes: 0.2 to 20 seconds.'],
  ['cruiseTime', PT.CRUISE_TIME, 'standard', (v) => `${sig(SecondsFromParam(V(v)))} s`, 'Drive Loop: time in normal space between round trips, 0.2 to 20 seconds.'],

  ['cruiseSpeed', PT.CRUISE_SPEED, 'standard', (v) => (CruiseFromParam(V(v)) === 0 ? 'still' : `${sig(CruiseFromParam(V(v)))} D/s`), 'How fast the stars drift in normal space, in D (the star field’s depth) per second: 0 holds them still (a jump still works, from rest), then 0.005 to 2, geometrically.'],
  ['topSpeed', PT.TOP_SPEED, 'standard', (v) => `×${sig(TopFromParam(V(v)))}`, 'The look’s speed in transit, a tenth to ten times its own.'],
  ['headingX', PT.HEADING_X, 'standard', (v) => signed(HeadingFromParam(V(v))), 'Where the vanishing point is across the frame: −1 the left edge, +1 the right.'],
  ['headingY', PT.HEADING_Y, 'standard', (v) => signed(HeadingFromParam(V(v))), 'Where the vanishing point is up the frame: −1 the bottom edge, +1 the top.'],
  ['fieldOfView', PT.FIELD_OF_VIEW, 'standard', (v) => `${fix(FieldOfViewFromParam(V(v)), 0)}°`, 'The vertical field of view, 20° to 120°. What it changes is the black hole’s sky, the tunnel and the clouds: the star field fills any view.'],
  ['shutter', PT.SHUTTER, 'standard', (v) => `${fix(360 * ShutterFromParam(V(v)), 0)}°`, 'The shutter’s opening, 0° to 360° of the frame: longer streaks, the same light (exposure is normalised to the shutter, as a camera on auto would).'],

  ['stars', PT.STARS, 'integer', null, 'How many stars in the box, 0 to 20,000. FF_TYPE_INTEGER in the plugin; a dropdown of fifteen of its values here, the default 4,000 among them.'],
  ['starColour', PT.STAR_COLOUR, 'standard', (v) => `${fix(100 * clamp01(V(v)), 0)}%`, 'White to each star’s own blackbody colour (3,000 to 30,000 K).'],
  ['exposure', PT.EXPOSURE, 'standard', (v) => `${signed(8 * clamp01(V(v)) - 4, 1)} EV`, 'The film’s exposure, ±4 stops. Everything is light until the composite develops it as film, 1 − exp(−E) per channel: bright cores burn out white and keep their colour at the edges.'],
  ['flash', PT.FLASH, 'standard', (v) => fix(V(v)), 'The look’s flashes: the jump’s, the warp’s core, streak and ring, the wormhole’s white-out, the white hole.'],
  ['tint', PT.TINT, 'standard', (v) => `hue ${fix(V(v))}`, 'The colour of the tunnel, the clouds and the warp flash: a hue wheel, lifted towards white, at unit luminance.'],

  ['tunnel', PT.TUNNEL, 'standard', (v) => fix(V(v)), 'Hyperspace: the later films’ mottled tunnel in transit, smeared along the line of flight by the same shutter. 0 leaves the steady lines alone.'],
  ['stretch', PT.STRETCH, 'standard', (v) => fix(StretchFromParam(V(v))), 'Warp: the rubber band’s pull towards the vanishing point, 0 to 0.95 of the radius at the core.'],
  ['dispersion', PT.DISPERSION, 'standard', (v) => fix(V(v)), 'Warp: the stars’ colours split along their streaks, the blue end running ahead of the red.'],
  ['swirl', PT.SWIRL, 'standard', (v) => `${fix(SwirlTurnsFromParam(V(v)), 1)} turns`, 'Wormhole: the whirlpool’s twist at its core, 0 to 6 turns.'],
  ['clouds', PT.CLOUDS, 'standard', (v) => fix(V(v)), 'Wormhole: how dense the three layers of swirling cloud are.'],

  ['holeSize', PT.HOLE_SIZE, 'standard', (v) => `${fix(HoleSizeFromParam(V(v)))} ×½fov`, 'Black Hole: the shadow’s angular radius, as a share of the vertical half field of view (0.05 to 0.9), held in transit.'],
  ['disc', PT.DISC, 'standard', (v) => fix(V(v)), 'Black Hole: the accretion disc’s light. A thin, hot, Keplerian disc; its far side is lensed up over the shadow and under it.'],
  ['discTilt', PT.DISC_TILT, 'standard', (v) => `${fix(DiscTiltFromParam(V(v)), 0)}°`, 'Black Hole: the disc’s inclination, 0° face on to 90° edge on.'],
  ['discSize', PT.DISC_SIZE, 'standard', (v) => `${fix(DiscOuterFromParam(V(v)), 1)} M`, 'Black Hole: the disc’s outer radius, 8 to 40 GM/c² (its inner edge is at 6, the innermost stable orbit).'],
  ['discHeat', PT.DISC_HEAT, 'standard', (v) => `${grouped(DiscHeatFromParam(V(v)))} K`, 'Black Hole: the hottest the disc gets, 2,000 to 40,000 K, geometrically; its light is a blackbody seen through the Doppler and gravitational shifts.'],
  ['doppler', PT.DOPPLER, 'standard', (v) => fix(V(v)), 'Black Hole: how much of the Doppler shift is shown. At 1 the side coming towards you is bluer and far brighter; at 0 it is off, as a well-known film’s makers switched it off on purpose.'],
  ['corona', PT.CORONA, 'standard', (v) => fix(V(v)), 'Black Hole: a faint glow round the hole, added up along each bent ray; the rays that skim the photon sphere pass through the most of it, so a thin photon ring appears at the shadow’s edge with nothing drawing it.'],

  ['clipDepth', PT.CLIP_DEPTH, 'standard', (v) => `${sig(ClipDepthFromParam(V(v)))} D`, 'How far ahead the clip rides, 0.02 to 2 D, geometrically: nearer smears more in hyperspace. (The one cheat: the picture never gets any nearer.)', null, 'over'],
  ['clipThrough', PT.CLIP_THROUGH, 'standard', (v) => fix(V(v)), 'How much of the clip you can still see in transit, and how much it colours the wormhole’s clouds.', null, 'over'],
  ['mix', PT.MIX, 'standard', (v) => fix(V(v)), 'The whole effect against the clip. At 0 the clip comes back as it went in.', null, 'over'],
];


const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';

// FF_TYPE_BOOLEAN and FF_TYPE_EVENT are the kit's toggles; FF_TYPE_INTEGER is a
// dropdown of values (see STAR_CHOICES).
const PARAMS = SPEC.map(([id, slot, ffType, display, hint, elements, only]) => {
  if (slot !== HostOrder(true)[slot] || NameOf(slot) === '?') throw new Error(`${id}: not a declared parameter`);
  const p = { id, slot, name: NameOf(slot), group: GroupOf(slot), hint, only };
  if (ffType === 'option') Object.assign(p, { type: 'option', elements });
  else if (ffType === 'integer') Object.assign(p, { type: 'option', elements: STAR_CHOICES.map((n) => grouped(n)) });
  else if (ffType === 'boolean' || ffType === 'event') p.type = 'boolean';
  else Object.assign(p, { type: 'standard', display });
  p.default = id === 'stars' ? STAR_CHOICES.indexOf(kDefaults[slot]) : kDefaults[slot];
  return p;
});

let renderer = null;

const mounted = mountDemo({
  name: 'Slipstream',
  pluginId: 'SR01 · SR02',
  kind: ['source', 'effect'],
  tagline:
    'Jumps to hyperspace, warp jumps, wormholes and a black hole, from one idea: a camera flying through space with its shutter open. Nothing draws a streak. A star sits at some depth off the line of flight, the camera moves on while the shutter is open, and its image runs out from the vanishing point for as long as the shutter lets it. Speed up and the runs get longer. The black hole is Schwarzschild’s, done properly: its shadow, a sky bent into arcs and an Einstein ring, a Keplerian disc lensed over and under it, and a photon ring nothing draws. SW Slipstream is space with stars in it; SW Slip Over puts your clip in the front window.',
  repo: 'https://github.com/stoatworks-labs/slipstream',

  blurb:
    'It is Slipstream’s own four GLSL passes, ported from the repository to WebGL2 unedited, fed by a JavaScript port of the plugin’s CPU half — the journey and its camera, every control’s mapping, the blackbody colour table and the black hole’s orbit tables, integrated here in double as the plugin does — which a check in the repository holds to the plugin’s C++, uniform for uniform. The plugin has no audio input, so nothing is left out for want of one. SW Slipstream reads no video; SW Slip Over runs on generated clips or your own.',

  // The source is transparent where space is empty; the backdrop shows it.
  showBackdrop: true,
  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Slipstream (source)', hint: 'SR01, FF_SOURCE: space, with stars in it. Reads no clip; transparent where space is empty.' },
      { id: 'over', name: 'SW Slip Over (effect)', hint: 'SR02, FF_EFFECT: your clip is the view out of the front window.' },
    ],
  },

  // For the Over only. The scene is a picture to fly into; the geometry card
  // makes the rubber band, the whirlpool and the black hole's lensing readable
  // (an Einstein ring of rings); lights on black show hyperspace's smear of a
  // bright point into an evenly lit line.
  sources: ['scene', 'grid', 'spot', 'bars', 'ramp', 'detail', 'alpha'],

  params: PARAMS,

  differences: [
    'The CPU half is a PORT, not the plugin’s code. The plugin flies its camera on the CPU in double (Flight.cpp: the journey, its four drives, the camera’s distance integrated across each frame, each look’s script), maps its controls there (Controls.cpp), and builds two tables there once: the blackbody colours (Blackbody.cpp: Planck’s law against a fit of the CIE colour matching functions) and the black hole’s light paths (Geodesic.cpp: 1,024 orbits integrated by RK4). Here all of that is JavaScript, whose numbers are doubles too. A check in the repository runs the port and the plugin’s own C++ side by side and compares what they print: every control’s mapping at every slider position, every look’s script, the drives through jumps abandoned halfway, the camera, both tables. With the plugin’s own libm results replayed into the port, all 90,546 lines are identical to the bit. On this page’s own Math, which differs from Apple’s libm in the last bit of about one result in ten, the doubles drift by a few units in the last place, and every float the GPU reads from the tables is still the same.',
    'The frame logic is a port too: the part of the plugin’s ProcessOpenGL that turns the controls and the clock into each pass’s uniforms (the shutter’s distance, the flash averaged over the open shutter, the whirlpool’s and the disc’s clocks, the sky’s star density, the black hole’s distance). The same check drives the real plugin class through 9,976 frames of both plugins in a GL context, reads every uniform back out of its four shader programs after every frame, and requires this page’s values to be the same floats; they are, all of them, even on this page’s own Math and against the plugin as built for Apple Silicon. It does not check this page’s GL calls (the binding, the blending, the buffers), which only a reader checks. Frames drawn here were compared once with the plugin’s own harness at the same settings (1280 × 720, 180 frames, held mid-journey): of ten cases across both plugins and all four looks, nine came out 99.95% to 99.995% identical with every other pixel within one level of 255. The tenth, the Over’s black hole held on the geometry card, is 99.97% identical; 48 of its 921,600 pixels differ by more than one level (13 by more than four, at most 11), on the disc’s hard outer edge, where a rounding difference decides disc or clip.',
    'The GPU half is not a port: the intake, star, scene and composite passes are the plugin’s own GLSL, on the same half-float buffers (WebGL2 needs EXT_color_buffer_float to render into them, and the page stops and says so without it), the blackbody table filtered and the orbit tables read with texelFetch as in the plugin. demo/tools/check_shaders.py fails the repository’s verify script if a character of them, or the way they are joined, drifts.',
    'Precision: GLSL ES 3.00 is not GLSL 4.10. The kit declares highp everywhere it can, but the star pass reads the blackbody table in its VERTEX shader, where GLSL ES’s default sampler precision is lowp and the kit does not override it; desktop drivers ignore that, a phone’s may not. Without OES_texture_float_linear the blackbody table falls back to half float, and the line under the picture says so.',
    'Nothing audio: the plugin has no audio input and no beat sync, so nothing is missing. (In Resolume a jump on the beat is Jump mapped to a beat.)',
    'Jump is FF_TYPE_EVENT in the plugin: here it is a toggle the page releases after one frame, one rising edge, which is what the plugin counts. Stars is FF_TYPE_INTEGER, 0 to 20,000: here a dropdown of fifteen of its values, the default 4,000 among them. The About block is not on the panel.',
    'Switching the Plugin dropdown is a new instance, as it would be in Resolume: the journey starts again from normal space and every control goes back to its default. SW Slip Over declares three more controls (Clip Depth, Clip Through, Mix), shown only when it is chosen.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame. The plugin’s vote on whether the host sends seconds or milliseconds is not ported, because this host’s unit is known. Restart is a clock jump, which the plugin takes as no time passing, so the journey carries on. Paused, no time passes, so nothing moves during the shutter and the streaks shrink to points, as the plugin draws a frame with no time in it; Step is one frame of a sixtieth of a second. A streak’s length is the distance flown while the shutter is open, so it depends on the frame rate here as in Resolume.',
    'The Wormhole is the expensive look (three layers of domain-warped noise a pixel): where a browser draws fewer frames a second than Resolume would, the kit’s tenth-of-a-second cap means the journey runs slower than its timings rather than jumping.',
    'Over: the clip is the kit’s generated one (or your own file), premultiplied and 8-bit as Resolume hands an effect its input. The harness’s night-city card is not here.',
    'The black hole’s orbit tables are built in this page at load, in double, as the plugin builds them on first use; the line under the picture says how long that took.',
    'Nothing here is measured. The plugin’s harness holds a star’s streak to a simulated open shutter within 1%, its light the same at rest as stretched across a hundred pixels, the black hole’s shadow within 0.06 px of Synge’s formula and an Einstein ring within 0.03 px of its own ray tracer, with seventeen deliberately wrong models and fourteen mutants all caught — and that harness, not this page, is the reason to believe the picture. The plugin itself has never been loaded into Resolume.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin; the Look picker; the status line.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule. Rows
// are found by their control's id, falling back to the name for the toggles,
// which carry no id.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin, since the two declare different controls.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(variant) {
    for (const p of PARAMS) {
      if (p.only) show(rowOf(p), p.only === variant);
    }
    // The Clip group's heading goes with its rows.
    for (const heading of document.querySelectorAll('.pgroup__name')) {
      if (heading.textContent === 'Clip') show(heading.closest('.pgroup'), variant === 'over');
    }
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: the constructor's defaults (the same for both).
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });

  if (!embed) {
    showForVariant(shownVariant);

    const stage = document.querySelector('.stage');
    if (stage) {
      //---------------------------------------------------------------------
      // The Look picker: the plugin's own Look control, as four buttons.
      //---------------------------------------------------------------------
      const picker = document.createElement('div');
      picker.className = 'transport';
      picker.setAttribute('role', 'group');
      picker.setAttribute('aria-label', 'Look');
      const label = document.createElement('span');
      label.className = 'transport__label';
      label.textContent = 'Look';
      label.title = 'The plugin’s Look control (it is also the first row of the panel)';
      picker.append(label);
      const buttons = LOOK_NAMES.map((name, index) => {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'btn';
        b.textContent = name;
        b.addEventListener('click', () => params.set('look', index));
        picker.append(b);
        return b;
      });
      const syncPicker = () => {
        const current = Math.round(params.get('look'));
        buttons.forEach((b, i) => {
          b.setAttribute('aria-pressed', String(i === current));
          b.style.outline = i === current ? '2px solid currentColor' : '';
        });
      };
      params.addEventListener('change', syncPicker);
      params.addEventListener('reset', syncPicker);
      syncPicker();
      stage.querySelector('.transport')?.after(picker);

      //---------------------------------------------------------------------
      // A status line: reports what the port computed, measures nothing.
      //---------------------------------------------------------------------
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'slipstream-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      const note = document.createElement('p');
      note.className = 'stage__status';
      note.id = 'slipstream-note';
      stage.append(note);
      const where = (j) => (j === 0 ? 'normal space' : j < 0.5 ? 'entering' : j === 0.5 ? 'in transit' : 'leaving');
      setInterval(() => {
        const s = renderer?.stats;
        if (!s) return;
        line.textContent =
          `${LOOK_NAMES[s.look]} · J ${s.J.toFixed(3)} (${where(s.J)})`
          + ` · ${sig(s.speed)} D/s · ${sig(s.total)} D flown`
          + ` · shutter ${s.shutter.toFixed(3)} D`
          + (s.look === Look.BlackHole ? ` · hole at ${sig(s.hole)} GM/c²` : ` · ${grouped(s.starCount)} stars`)
          + (s.flash > 0.001 ? ` · flash ${s.flash.toFixed(2)}` : '');
        note.textContent =
          `Orbit and colour tables built in this page in ${Math.round(renderer.tableMs)} ms.`
          + (renderer.floatLinear ? '' : ' This browser has no OES_texture_float_linear, so the blackbody table is half float here, not the plugin’s 32-bit float.');
      }, 250);
    }
  }

  // For the headless checks in AGENTS.md: the page's pieces, read-only by convention.
  window.slipstreamDemo = { mounted, get renderer() { return renderer; }, PARAMS, STAR_CHOICES, PT, createRenderer };
}
