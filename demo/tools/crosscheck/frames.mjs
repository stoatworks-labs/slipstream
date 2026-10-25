// The JavaScript half of the browser demo's FRAME cross-check: demo/plugin.js
// from its BEGIN PORT marker to its END FRAME marker (sliced out as text and
// run here, so what is checked is exactly what the page runs), driven through
// scenario.mjs's frames, printing what uniforms.cpp prints from the real
// plugin: the CPU state after each frame, and every uniform each of the four
// programs holds -- a program keeps a uniform's value until a pass sets it
// again, so a pass that does not run this frame (the stars under the Black
// Hole, the intake in the source) prints what it was last given. A uniform the
// port has never set prints nothing here; compare.py requires the plugin's to
// be GL's initial zero.
//
//     node demo/tools/crosscheck/frames.mjs < scenario.txt > js-frames.txt
//     LIBM_REPLAY=calls.bin ...        the plugin's libm results, replayed

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadReplay, replayMath } from './replay.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'plugin.js'), 'utf8');
const begin = source.indexOf('// ==== BEGIN PORT');
const end = source.indexOf('// ==== END FRAME');
if (begin < 0 || end < begin || source.indexOf('// ==== BEGIN FRAME') < begin) {
  process.stderr.write('frames.mjs: the PORT and FRAME markers are missing from demo/plugin.js\n');
  process.exit(2);
}
const replay = process.env.LIBM_REPLAY ? loadReplay(process.env.LIBM_REPLAY) : null;
const { math, misses } = replayMath(replay);
// eslint-disable-next-line no-new-func
const P = new Function('Math', `${source.slice(begin, end)}\nreturn { SlipstreamFrames, kDefaults, kParamSlots, ClipLevelsFor, HostOrder };`)(math);

const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const d = (v) => { view.setFloat64(0, v); return h32(view.getUint32(0)) + h32(view.getUint32(4)); };
const f = (v) => { view.setFloat32(0, v); return h32(view.getUint32(0)); };
const fromBits32 = (hex) => { view.setUint32(0, parseInt(hex, 16)); return view.getFloat32(0); };
const fromBits64 = (hex) => { view.setUint32(0, parseInt(hex.slice(0, 8), 16)); view.setUint32(4, parseInt(hex.slice(8), 16)); return view.getFloat64(0); };

const out = [];
let inst = null;
let params = null;
let declared = null;
let width = 0;
let height = 0;
let state = null;

for (const line of readFileSync(0, 'utf8').split('\n')) {
  const words = line.trim().split(/\s+/);
  const verb = words[0];
  if (verb === 'plugin') {
    const effect = words[1] === 'over';
    [width, height] = words[2].split('x').map(Number);
    inst = new P.SlipstreamFrames(effect);
    // The constructor's params[]: every slot, declared by this plugin or not.
    params = new Float32Array(P.kParamSlots);
    for (let id = 0; id < P.kParamSlots; id += 1) params[id] = P.kDefaults[id];
    declared = new Set(P.HostOrder(effect));
    state = { intake: new Map(), stars: new Map(), scene: new Map(), composite: new Map() };
    out.push(`P ${words[1]} ${width}x${height}`);
  } else if (verb === 'raster') {
    [width, height] = words[1].split('x').map(Number);
    out.push(`R ${width}x${height}`);
  } else if (verb === 'set') {
    // SetById: a control this plugin does not declare has no host index.
    const id = Number(words[1]);
    if (declared.has(id)) params[id] = fromBits32(words[2]);
  } else if (verb === 'frame') {
    const frame = Number(words[1]);
    const now = fromBits64(words[2]);
    const clipLevels = inst.isEffect ? P.ClipLevelsFor(width, height) : 1;
    const { passes } = inst.frame(params, width, height, now, clipLevels);
    out.push(`C ${frame} dt=${d(inst.frameDt)} J=${d(inst.journey.Phase())} start=${d(inst.journey.Start())} end=${d(inst.journey.End())} total=${d(inst.camera.Total())} pz=${d(inst.camera.PhaseZ())} lz=${d(inst.camera.LongZ())} speed=${d(inst.camera.Speed())} shutter=${d(inst.shutterZ)} focal=${d(inst.focal)} flash=${d(inst.flashLight)} hole=${d(inst.holeD)} disc=${d(inst.discLight)} corona=${d(inst.coronaLight)}`);
    for (const name of ['intake', 'stars', 'scene', 'composite']) {
      const held = state[name];
      for (const [uniform, kind, values] of passes[name] ?? []) {
        held.set(uniform, `${kind}:${values.map((v) => (kind === 'f' ? f(v) : h32(v))).join(',')}`);
      }
      const fields = [...held].map(([k, v]) => `${k}=${v}`).sort();
      out.push(`U ${frame} ${name}${fields.map((x) => ` ${x}`).join('')}`);
    }
  } else if (verb) {
    process.stderr.write(`frames.mjs: unknown '${verb}'\n`);
    process.exit(2);
  }
}

process.stdout.write(`${out.join('\n')}\n`);
if (replay) process.stderr.write(`${JSON.stringify({ replayed: replay.calls, misses })}\n`);
