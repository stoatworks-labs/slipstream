// The JavaScript half of the browser demo's cross-check of its CPU port: the
// PORT block of demo/plugin.js (between its BEGIN PORT and END PORT markers,
// sliced out as text and run here, so what is checked is exactly what the page
// runs), driven through printer.cpp's scenario and printed the same way.
// crosscheck.sh diffs the two outputs. Read this beside printer.cpp: each block
// mirrors one there.
//
//     node demo/tools/crosscheck/run.mjs > js.txt

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadReplay, replayMath } from './replay.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'plugin.js'), 'utf8');
const begin = source.indexOf('// ==== BEGIN PORT');
const end = source.indexOf('// ==== END PORT');
if (begin < 0 || end < begin) {
  process.stderr.write('run.mjs: the PORT markers are missing from demo/plugin.js\n');
  process.exit(2);
}
const names = [
  'PT', 'HostOrder', 'GroupOf', 'NameOf', 'LOOK_NAMES', 'DRIVE_NAMES', 'Drive', 'OptionIndex',
  'SecondsFromParam', 'ParamFromSeconds', 'CruiseFromParam', 'ParamFromCruise', 'TopFromParam', 'ParamFromTop',
  'HeadingFromParam', 'FieldOfViewFromParam', 'ParamFromFieldOfView', 'ShutterFromParam', 'ExposureFromParam',
  'ParamFromExposure', 'StretchFromParam', 'SwirlTurnsFromParam', 'HoleSizeFromParam', 'DiscTiltFromParam',
  'DiscOuterFromParam', 'DiscHeatFromParam', 'ParamFromDiscHeat', 'ClipDepthFromParam', 'ParamFromClipDepth',
  'TransitSpeed', 'PoseAt', 'FlashPoints', 'FlashDecay', 'ShadowAngle', 'DistanceForShadow', 'Journey', 'Camera',
  'BlackbodyTable', 'GeodesicTables', 'EscapeB', 'CaptureB', 'kB', 'kT',
];
// LIBM_REPLAY=calls.bin: the port's libm calls answered from the plugin's own
// (see replay.mjs and libm_record.c). Without it, the port runs on V8's Math,
// as the page does.
const replay = process.env.LIBM_REPLAY ? loadReplay(process.env.LIBM_REPLAY) : null;
const { math, misses, compare } = replayMath(replay);
// eslint-disable-next-line no-new-func
const P = new Function('Math', `${source.slice(begin, end)}\nreturn { ${names.join(', ')} };`)(math);

const out = [];
const print = (line) => out.push(line);

const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const f = (v) => { view.setFloat32(0, v); return h32(view.getUint32(0)); };
const d = (v) => { view.setFloat64(0, v); return h32(view.getUint32(0)) + h32(view.getUint32(4)); };
const bitsOf = (v) => { view.setFloat32(0, v); return view.getUint32(0); };
const fnv = (h, word) => Math.imul((h ^ word) >>> 0, 16777619) >>> 0;
const F = Math.fround;
const kPi = 3.14159265358979323846;

const poseFields = ['speed', 'tunnel', 'pull', 'clipScale', 'twist', 'mouth', 'endLight', 'endGlow', 'holeD', 'disc', 'blackout', 'scene'];
const pose = (p) => poseFields.map((k) => d(p[k])).join(' ');

function hostAt(scenario, frame) {
  const h = { drive: 0, engage: false, jump: false, fader: 0.0 };
  switch (scenario % 4) {
    case 0:
      h.drive = P.Drive.Hold;
      h.engage = (frame >= 10 && frame < 230) || (frame >= 262 && frame < 300) || (frame >= 520 && frame < 900);
      break;
    case 1:
      h.drive = P.Drive.Trip;
      h.jump = frame === 5 || frame === 6 || frame === 40 || frame === 420 || frame === 421 || frame === 700 || frame === 1100;
      break;
    case 2:
      h.drive = P.Drive.Loop;
      break;
    default:
      h.drive = P.Drive.Fader;
      if (frame < 300) h.fader = frame / 300.0;
      else if (frame < 500) h.fader = (600 - frame) / 300.0;
      else if (frame < 700) h.fader = (frame - 450) / 150.0 - 0.25;
      else h.fader = (frame % 7) / 6.0;
      break;
  }
  return h;
}

//---------------------------------------------------------------------------
// Controls.
//---------------------------------------------------------------------------
for (let k = -10; k <= 1010; k += 1) {
  const v = F(k / 1000.0);
  print(`ctl ${k} ${f(v)} sec=${d(P.SecondsFromParam(v))} cru=${d(P.CruiseFromParam(v))} top=${d(P.TopFromParam(v))} head=${d(P.HeadingFromParam(v))} fov=${d(P.FieldOfViewFromParam(v))} shu=${d(P.ShutterFromParam(v))} exp=${d(P.ExposureFromParam(v))} str=${d(P.StretchFromParam(v))} swi=${d(P.SwirlTurnsFromParam(v))} hole=${d(P.HoleSizeFromParam(v))} tilt=${d(P.DiscTiltFromParam(v))} outer=${d(P.DiscOuterFromParam(v))} heat=${d(P.DiscHeatFromParam(v))} clip=${d(P.ClipDepthFromParam(v))}`);
}
for (let k = -40; k <= 440; k += 1) {
  const v = F(k / 100.0);
  print(`opt ${k} ${P.OptionIndex(v, 4)} ${P.OptionIndex(v, 2)} ${P.OptionIndex(-v, 4)}`);
}
for (let k = -5; k <= 260; k += 1) {
  const x = k / 10.0;
  print(`inv ${k} sec=${f(P.ParamFromSeconds(x))} cru=${f(P.ParamFromCruise(x / 100.0))} top=${f(P.ParamFromTop(x / 2.0))} fov=${f(P.ParamFromFieldOfView(5.0 * x))} exp=${f(P.ParamFromExposure(x / 2.0 - 5.0))} heat=${f(P.ParamFromDiscHeat(1600.0 * x))} clip=${f(P.ParamFromClipDepth(x / 10.0))}`);
}
print(`defaults entry=${f(P.ParamFromSeconds(2.0))} transit=${f(P.ParamFromSeconds(3.0))} exit=${f(P.ParamFromSeconds(1.5))} cruise=${f(P.ParamFromSeconds(3.0))} cruiseSpeed=${f(P.ParamFromCruise(0.08))} top=${f(P.ParamFromTop(1.0))} fov=${f(P.ParamFromFieldOfView(60.0))} exposure=${f(P.ParamFromExposure(0.0))} heat=${f(P.ParamFromDiscHeat(4500.0))} clipDepth=${f(P.ParamFromClipDepth(0.3))}`);
for (let effect = 0; effect < 2; effect += 1) print(`order ${effect} ${P.HostOrder(effect === 1).join(' ')}`);
for (let id = 0; id < P.PT.ABOUT_TEXT; id += 1) print(`param ${id} ${P.NameOf(id)}|${P.GroupOf(id)}`);
for (let i = 0; i < 4; i += 1) print(`names ${i} ${P.LOOK_NAMES[i]}|${P.DRIVE_NAMES[i]} transit=${d(P.TransitSpeed(i))} decay=${d(P.FlashDecay(i))}`);
for (let i = 0; i < 4; i += 1) print(`flashes ${i}${P.FlashPoints(i).map((p) => ` ${d(p)}`).join('')}`);

//---------------------------------------------------------------------------
// The black hole's distance for a shadow, and the shadow for a distance.
//---------------------------------------------------------------------------
for (let k = -2; k <= 400; k += 1) print(`shadow ${k} ${d(P.ShadowAngle(2.5 + k * k * 0.25))}`);
for (let k = 0; k <= 400; k += 1) print(`dist ${k} ${d(P.DistanceForShadow(1.0e-6 + k * (1.5707963 / 400.0)))}`);

//---------------------------------------------------------------------------
// Each look's script.
//---------------------------------------------------------------------------
print(`posefields ${poseFields.join(' ')}`);
const holds = [1.0, P.DistanceForShadow(0.3 * 0.5 * 60.0 * kPi / 180.0), 4000.0, 2.0e5];
for (let look = 0; look < 4; look += 1) {
  for (let s = 0; s < 4; s += 1) {
    const settings = { stretch: 0.2 * s + 0.1, holeHold: holds[s] };
    for (let k = -100; k <= 1100; k += 1) print(`pose ${look} ${s} ${k} ${pose(P.PoseAt(look, k / 1000.0, settings))}`);
    const exact = [0.0, 0.5, 1.0, 0.25, 0.75, 0.4, 0.575, 0.325, 0.65, 0.8, 0.999999999, 0.5000000000000001];
    for (const J of exact) print(`posex ${look} ${s} ${d(J)} ${pose(P.PoseAt(look, J, settings))}`);
  }
}

//---------------------------------------------------------------------------
// The drive and the camera, frame by frame.
//---------------------------------------------------------------------------
const rates = [60.0, 50.0, 30.0];
for (let scenario = 0; scenario < 4 * 4 * 3; scenario += 1) {
  const look = Math.trunc(scenario / 4) % 4;
  const fps = rates[Math.trunc(scenario / 16)];
  const journey = new P.Journey();
  const camera = new P.Camera();
  const settings = { stretch: 0.7125, holeHold: holds[1] };
  const input = {
    drive: 0, engage: false, jumpPress: false, fader: 0.0,
    entry: P.SecondsFromParam(F(0.2 + 0.05 * (scenario % 5))),
    transit: P.SecondsFromParam(F(0.25 + 0.04 * (scenario % 7))),
    exit: P.SecondsFromParam(F(0.15 + 0.06 * (scenario % 3))),
    cruise: P.SecondsFromParam(F(0.2 + 0.03 * (scenario % 4))),
  };
  const cruise = P.CruiseFromParam(F(0.05 * (scenario % 6)));
  const top = P.TopFromParam(F(0.3 + 0.1 * (scenario % 5)));
  let held = false;
  for (let frame = 0; frame < 1300; frame += 1) {
    const h = hostAt(scenario, frame);
    input.drive = h.drive;
    input.engage = h.engage;
    input.jumpPress = h.jump && !held;
    held = h.jump;
    input.fader = h.fader;
    const dt = frame === 0 ? 0.0 : (frame / fps) - ((frame - 1) / fps);
    journey.Advance(dt, input);
    camera.Advance(look, journey.Start(), journey.End(), dt, cruise, top, settings);
    print(`fly ${scenario} ${frame} ${d(journey.Start())} ${d(journey.End())} ${d(journey.Phase())} ${d(camera.Total())} ${d(camera.PhaseZ())} ${d(camera.LongZ())} ${d(camera.Speed())} ${d(camera.OffsetAt(0.0))} ${d(camera.OffsetAt(0.37))} ${d(camera.OffsetAt(1.0 - 0.5))}`);
  }
}

for (const fps of rates) {
  for (let n = Math.ceil(0.2 * fps); n <= Math.trunc(5.0 * fps); n += 1) {
    const journey = new P.Journey();
    const input = { drive: P.Drive.Trip, engage: false, jumpPress: false, fader: 0.0, entry: 0.5, exit: 0.5, transit: n / fps, cruise: 4.0 };
    let back = -1;
    let h = 2166136261;
    for (let frame = 0; frame < 2000 && back < 0; frame += 1) {
      input.jumpPress = frame === 1;
      const dt = frame === 0 ? 0.0 : (frame / fps) - ((frame - 1) / fps);
      journey.Advance(dt, input);
      view.setFloat64(0, journey.Phase());
      h = fnv(fnv(h, view.getUint32(4)), view.getUint32(0));
      if (frame > 1 && journey.Phase() === 0.0) back = frame;
    }
    print(`trip ${fps} ${n} back=${back} h=${h32(h)}`);
  }
}

for (const fps of rates) {
  for (let n = Math.ceil(0.2 * fps); n <= Math.trunc(5.0 * fps); n += 1) {
    const journey = new P.Journey();
    const input = { drive: P.Drive.Loop, engage: false, jumpPress: false, fader: 0.0, entry: 0.5, exit: 0.5, transit: 0.5, cruise: n / fps };
    const left = [-1, -1];
    let trips = 0;
    let atRest = true;
    let h = 2166136261;
    for (let frame = 0; frame < 4000 && trips < 2; frame += 1) {
      const dt = frame === 0 ? 0.0 : (frame / fps) - ((frame - 1) / fps);
      journey.Advance(dt, input);
      const p = journey.Phase();
      view.setFloat64(0, p);
      h = fnv(fnv(h, view.getUint32(4)), view.getUint32(0));
      if (atRest && p !== 0.0) { left[trips] = frame; trips += 1; }
      atRest = p === 0.0;
    }
    print(`loop ${fps} ${n} left=${left[0]},${left[1]} h=${h32(h)}`);
  }
}

//---------------------------------------------------------------------------
// The blackbody table.
//---------------------------------------------------------------------------
const bb = P.BlackbodyTable();
for (let i = 0; i < 256; i += 1) print(`bb ${i} ${f(bb[i * 4])} ${f(bb[i * 4 + 1])} ${f(bb[i * 4 + 2])} ${f(bb[i * 4 + 3])}`);

//---------------------------------------------------------------------------
// The orbit tables.
//---------------------------------------------------------------------------
const t = P.GeodesicTables();
let all = 2166136261;
for (let i = 0; i < P.kB; i += 1) {
  let row = 2166136261;
  for (let j = 0; j < P.kT * 4; j += 1) row = fnv(row, bitsOf(t.orbits[i * P.kT * 4 + j]));
  all = fnv(all, row);
  print(`geo ${i} b=${d(P.EscapeB(i))} ${d(P.CaptureB(i))} uEnd=${d(t.uEndEscape[i])} phiEnd=${d(t.phiEndEscape[i])} phiEndCap=${d(t.phiEndCapture[i])} ends=${f(t.ends[i * 4])},${f(t.ends[i * 4 + 1])},${f(t.ends[i * 4 + 2])},${f(t.ends[i * 4 + 3])} row=${h32(row)}`);
}
for (let k = 0; k < 4000; k += 1) {
  const at = (k * 2654435761) % (P.kB * P.kT * 4);
  print(`orb ${at} ${f(t.orbits[at])}`);
}
print(`geoall ${h32(all)}`);

// Every float the GPU reads from the tables, raw, as printer.cpp writes them.
if (process.env.TABLES_OUT) {
  const parts = [t.orbits, t.ends, bb];
  writeFileSync(process.env.TABLES_OUT, Buffer.concat(parts.map((a) => Buffer.from(a.buffer, a.byteOffset, a.byteLength))));
}

process.stdout.write(`${out.join('\n')}\n`);
if (replay) {
  process.stderr.write(`${JSON.stringify({ replayed: replay.calls, misses, libm: compare() })}\n`);
}
