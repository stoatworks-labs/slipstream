// The frame cross-check's scenario: what a host does to the plugin, frame by
// frame, written out ONCE so the C++ (uniforms.cpp) and the port (frames.mjs)
// read the same thing. Every number is written as its bits, so neither side
// parses a decimal.
//
//     node demo/tools/crosscheck/scenario.mjs > scenario.txt
//
// Lines:
//   plugin source|over WxH    a new instance (InitGL), at that raster
//   raster WxH                the host's raster changes under it
//   set ID BITS32             SetFloatParameter by ParamId (the Over's host
//                             index is its ParamId; the source declares 0..31)
//   frame N BITS64            SetTime( t ), then ProcessOpenGL; print
//
// It walks every look through a whole trip on each drive, with the controls
// that feed a uniform moved on the way, and the clock doing what hosts do:
// jitter, a stall, a step back, a jump of more than a second, 30 fps patches.

const view = new DataView(new ArrayBuffer(8));
const b32 = (v) => { view.setFloat32(0, v); return view.getUint32(0).toString(16).padStart(8, '0'); };
const b64 = (v) => { view.setFloat64(0, v); return view.getUint32(0).toString(16).padStart(8, '0') + view.getUint32(4).toString(16).padStart(8, '0'); };

const PT = {
  LOOK: 0, DRIVE: 1, ENGAGE: 2, JUMP: 3, JOURNEY: 4, ENTRY_TIME: 5, TRANSIT_TIME: 6, EXIT_TIME: 7, CRUISE_TIME: 8,
  CRUISE_SPEED: 9, TOP_SPEED: 10, HEADING_X: 11, HEADING_Y: 12, FIELD_OF_VIEW: 13, SHUTTER: 14,
  STARS: 15, STAR_COLOUR: 16, EXPOSURE: 17, FLASH: 18, TINT: 19, TUNNEL: 20, STRETCH: 21, DISPERSION: 22,
  SWIRL: 23, CLOUDS: 24, HOLE_SIZE: 25, DISC: 26, DISC_TILT: 27, DISC_SIZE: 28, DISC_HEAT: 29, DOPPLER: 30, CORONA: 31,
  CLIP_DEPTH: 32, CLIP_THROUGH: 33, MIX: 34,
};

const lines = [];
let frame = 0;
let time = 0.0;
/** A pseudo-random jitter in integers, the same every run. */
let seed = 12345;
const rand = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed / 4294967296; };

function plugin(kind, w, h) { lines.push(`plugin ${kind} ${w}x${h}`); frame = 0; time = 0.0; }
function raster(w, h) { lines.push(`raster ${w}x${h}`); }
function set(id, v) { lines.push(`set ${id} ${b32(v)}`); }
function tick(dt) { lines.push(`frame ${frame} ${b64(time)}`); frame += 1; time += dt; }
/** n frames at a host's ~60 fps: a step of 1/60 s, jittered by up to a millisecond. */
function run(n, fps = 60) {
  for (let i = 0; i < n; i += 1) tick(1 / fps + (rand() - 0.5) * 0.002);
}
function press(id) { set(id, 1); tick(1 / 60); set(id, 0); }

function journeyThroughEveryLook(effect) {
  // Defaults: Hyperspace on Loop -- a whole round trip is 9.5 s.
  run(620);
  // Warp, with the shutter, the heading and the view moved mid-trip.
  set(PT.LOOK, 1); run(250);
  set(PT.SHUTTER, 0.83); set(PT.HEADING_X, 0.31); set(PT.HEADING_Y, 0.66); run(200);
  set(PT.FIELD_OF_VIEW, 0.72); set(PT.STRETCH, 0.4); set(PT.DISPERSION, 1.0); run(200);
  // The Wormhole, with its own controls and the light.
  set(PT.LOOK, 2); set(PT.SWIRL, 0.9); set(PT.CLOUDS, 0.3); set(PT.TINT, 0.15); run(300);
  set(PT.EXPOSURE, 0.7); set(PT.TOP_SPEED, 0.8); set(PT.STAR_COLOUR, 1.0); run(300);
  // The Black Hole: the hole held, its disc and corona, the plunge.
  set(PT.LOOK, 3); set(PT.HOLE_SIZE, 0.6); run(250);
  set(PT.DISC_TILT, 0.4); set(PT.DISC_SIZE, 0.9); set(PT.DISC_HEAT, 0.8); set(PT.DOPPLER, 0.25); set(PT.CORONA, 1.0); run(250);
  set(PT.STARS, 12000); set(PT.FLASH, 1.0); run(200);
  if (effect) { set(PT.CLIP_DEPTH, 0.15); set(PT.CLIP_THROUGH, 0.7); set(PT.MIX, 0.6); }
  // Back to Hyperspace for the tunnel and the Over's smear.
  set(PT.LOOK, 0); set(PT.TUNNEL, 1.0); set(PT.STARS, 0); run(150);
  set(PT.STARS, 20000); set(PT.CRUISE_SPEED, 0.0); run(150);
  if (effect) { set(PT.CLIP_DEPTH, 0.9); run(120); }
}

function drives() {
  // Hold: in, out halfway through the entry (it carries on), in, out.
  set(PT.DRIVE, 0); set(PT.LOOK, 1); set(PT.ENTRY_TIME, 0.45); set(PT.EXIT_TIME, 0.3); run(30);
  set(PT.ENGAGE, 1); run(40); set(PT.ENGAGE, 0); run(140); set(PT.ENGAGE, 1); run(200); set(PT.ENGAGE, 0); run(160);
  // Trip: presses, one while a trip is under way.
  set(PT.DRIVE, 1); set(PT.LOOK, 2); set(PT.TRANSIT_TIME, 0.3); run(20);
  press(PT.JUMP); run(60); press(PT.JUMP); run(400); press(PT.JUMP); run(300);
  // Fader: scrubbed in, held, scrubbed back through the flash points, past both ends.
  set(PT.DRIVE, 3); set(PT.LOOK, 0);
  for (let k = 0; k <= 60; k += 1) { set(PT.JOURNEY, k / 60); tick(1 / 60); }
  run(30);
  for (let k = 60; k >= 0; k -= 2) { set(PT.JOURNEY, k / 60); tick(1 / 60); }
  set(PT.LOOK, 3);
  for (let k = 0; k <= 40; k += 1) { set(PT.JOURNEY, 0.3 + k / 50); tick(1 / 60); }
  set(PT.LOOK, 1); set(PT.JOURNEY, 0.32); tick(1 / 60); set(PT.JOURNEY, 0.66); tick(1 / 60); set(PT.JOURNEY, 0.2); tick(1 / 60);
  // A fader that wraps: 0.98 to 0.03 is forward through normal space, and so
  // is 0.9 to 0.3 (back more than half a trip has gone round); 0.6 to 0.2 is back.
  for (const j of [0.98, 0.03, 0.9, 0.3, 0.6, 0.2, 1.0, 0.0]) { set(PT.JOURNEY, j); tick(1 / 60); }
  // The Wormhole's way out and its fading white-out, then the white hole
  // crossed at a different point of each frame.
  set(PT.LOOK, 2);
  for (let k = 0; k <= 40; k += 1) { set(PT.JOURNEY, 0.7 + k * 0.0075); tick(1 / 60); }
  set(PT.LOOK, 3); set(PT.JOURNEY, 0.7); tick(1 / 60);
  for (const j of [0.79, 0.805, 0.79, 0.8125, 0.79, 0.85]) { set(PT.JOURNEY, j); tick(1 / 60); }
  // Loop again, with the clock misbehaving: 30 fps, a stall, a step back, a
  // jump of more than a second, irregular steps.
  set(PT.DRIVE, 2); set(PT.LOOK, 0); set(PT.CRUISE_TIME, 0.1);
  run(120, 30);
  tick(0); tick(0); tick(1 / 60);
  time -= 0.4; run(20);
  time += 3.0; run(20);
  for (let i = 0; i < 200; i += 1) tick(0.002 + rand() * 0.09);
}

plugin('source', 320, 180);
journeyThroughEveryLook(false);
raster(1280, 720); run(60);
raster(333, 187); run(60);
drives();

plugin('over', 320, 180);
journeyThroughEveryLook(true);
raster(640, 360); run(60);
raster(333, 187); run(60);
drives();

process.stdout.write(`${lines.join('\n')}\n`);
