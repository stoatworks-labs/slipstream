// The libm replay for the cross-check: a stand-in for JavaScript's Math whose
// transcendental functions answer from a record of the plugin's own libm calls
// (libm_record.c), so the port can be compared with the C++ to the bit. See
// libm_record.c for why. Everything that is not a libm call in the plugin
// (floor, round, trunc, fround, min, abs, sqrt -- exact, or the same IEEE
// operation either side) is Math's own.
//
// A call the record does not hold (an argument the C++ never passed, which is
// what a port error looks like, or a constant the compiler folded at build time
// so that it never reached libm at run time) falls back to Math's and is
// counted: `misses`.

import { readFileSync } from 'node:fs';

const NAMES = { 1: 'exp', 2: 'expm1', 3: 'log', 4: 'log2', 5: 'exp2', 6: 'sin', 7: 'cos', 8: 'tan', 9: 'asin', 10: 'atan', 11: 'pow', 12: 'hypotf', 13: 'sincos' };

export function loadReplay(path) {
  const buf = readFileSync(path);
  const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const maps = Object.fromEntries(Object.values(NAMES).map((n) => [n, new Map()]));
  let calls = 0;
  let at = 0;
  const key64 = (o) => `${view.getUint32(o + 4, true).toString(16)}:${view.getUint32(o, true).toString(16)}`;
  while (at < buf.byteLength) {
    const fn = NAMES[view.getUint8(at)];
    at += 1;
    calls += 1;
    if (fn === 'pow') {
      maps.pow.set(`${key64(at)}/${key64(at + 8)}`, view.getFloat64(at + 16, true));
      at += 24;
    } else if (fn === 'hypotf') {
      maps.hypotf.set(`${view.getUint32(at, true).toString(16)}/${view.getUint32(at + 4, true).toString(16)}`, view.getFloat32(at + 8, true));
      at += 12;
    } else if (fn === 'sincos') {
      maps.sincos.set(key64(at), [view.getFloat64(at + 8, true), view.getFloat64(at + 16, true)]);
      at += 24;
    } else if (fn) {
      maps[fn].set(key64(at), view.getFloat64(at + 8, true));
      at += 16;
    } else {
      throw new Error(`replay: a record with an unknown function at byte ${at - 1}`);
    }
  }
  return { maps, calls };
}

const scratch = new DataView(new ArrayBuffer(8));
const k64 = (x) => { scratch.setFloat64(0, x, true); return `${scratch.getUint32(4, true).toString(16)}:${scratch.getUint32(0, true).toString(16)}`; };
const k32 = (x) => { scratch.setFloat32(0, x, true); return scratch.getUint32(0, true).toString(16); };

/**
 * A Math whose libm functions answer from `replay` (or Math's own when no
 * replay is given). Returns { math, misses, compare }: `misses` counts calls by
 * function with no record; `compare()` measures how Math's own results differ
 * from the record's, call by distinct call.
 */
export function replayMath(replay) {
  const misses = {};
  const math = Object.create(Math);
  if (!replay) return { math, misses, compare: () => null };
  const { maps } = replay;
  const miss = (fn, own) => { misses[fn] = (misses[fn] ?? 0) + 1; return own; };
  const one = (fn) => (x) => {
    const r = maps[fn].get(k64(x));
    return r === undefined ? miss(fn, Math[fn](x)) : r;
  };
  for (const fn of ['exp', 'expm1', 'log', 'log2', 'tan', 'asin', 'atan']) math[fn] = one(fn);
  math.sin = (x) => {
    const key = k64(x);
    const r = maps.sin.get(key) ?? maps.sincos.get(key)?.[0];
    return r === undefined ? miss('sin', Math.sin(x)) : r;
  };
  math.cos = (x) => {
    const key = k64(x);
    const r = maps.cos.get(key) ?? maps.sincos.get(key)?.[1];
    return r === undefined ? miss('cos', Math.cos(x)) : r;
  };
  // std::exp2 is written Math.pow( 2.0, x ) in the port: JavaScript has no exp2.
  math.pow = (x, y) => {
    const r = maps.pow.get(`${k64(x)}/${k64(y)}`) ?? (x === 2 ? maps.exp2.get(k64(y)) : undefined);
    return r === undefined ? miss('pow', Math.pow(x, y)) : r;
  };
  // std::hypot of two floats is hypotf; the port writes f32( Math.hypot( a, b ) ).
  math.hypot = (x, y) => {
    const r = maps.hypotf.get(`${k32(x)}/${k32(y)}`);
    return r === undefined ? miss('hypotf', Math.hypot(x, y)) : r;
  };

  /** How V8's Math differs from the plugin's libm over every distinct recorded call. */
  const compare = () => {
    const ulp64 = (a, b) => {
      if (a === b || (Number.isNaN(a) && Number.isNaN(b))) return 0;
      const v = new DataView(new ArrayBuffer(16));
      v.setFloat64(0, a);
      v.setFloat64(8, b);
      const ia = v.getBigInt64(0);
      const ib = v.getBigInt64(8);
      const d = ia > ib ? ia - ib : ib - ia;
      return d > 1000000n ? Infinity : Number(d);
    };
    const parse = (key) => {
      const [hi, lo] = key.split(':').map((h) => parseInt(h, 16));
      scratch.setUint32(4, hi, true);
      scratch.setUint32(0, lo, true);
      return scratch.getFloat64(0, true);
    };
    const out = {};
    for (const [fn, map] of Object.entries(maps)) {
      let n = 0;
      let differ = 0;
      let worst = 0;
      for (const [key, r] of map) {
        n += 1;
        let own;
        if (fn === 'pow') { const [a, b] = key.split('/').map(parse); own = Math.pow(a, b); }
        else if (fn === 'hypotf') {
          const [a, b] = key.split('/').map((h) => { scratch.setUint32(0, parseInt(h, 16), true); return scratch.getFloat32(0, true); });
          // In float ulps: the plugin's hypotf against the port's f32( Math.hypot ).
          const v = new DataView(new ArrayBuffer(8));
          v.setFloat32(0, Math.fround(Math.hypot(a, b)));
          v.setFloat32(4, r);
          const u = Math.abs(v.getInt32(0) - v.getInt32(4));
          if (u) { differ += 1; worst = Math.max(worst, u); }
          continue;
        } else if (fn === 'sincos') {
          const x = parse(key);
          const u = Math.max(ulp64(Math.sin(x), r[0]), ulp64(Math.cos(x), r[1]));
          if (u) { differ += 1; worst = Math.max(worst, u); }
          continue;
        } else if (fn === 'exp2') own = Math.pow(2, parse(key));
        else own = Math[fn](parse(key));
        const u = ulp64(own, r);
        if (u) { differ += 1; worst = Math.max(worst, u); }
      }
      if (n) out[fn] = { distinct: n, differ, worstUlps: worst };
    }
    return out;
  };

  return { math, misses, compare };
}
