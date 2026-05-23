// |jit-test| skip-if: !wasmSimdEnabled()

// replace_lane fed by extract_lane, for every lane size pairing, including the
// ones where the extracted value is sign- or zero-extended into the replaced
// lane and the ones reading from the vector being replaced into.

const extracts = [
  ["i8x16.extract_lane_s", 1, true],
  ["i8x16.extract_lane_u", 1, false],
  ["i16x8.extract_lane_s", 2, true],
  ["i16x8.extract_lane_u", 2, false],
  ["i32x4.extract_lane", 4, true],
];
const replaces = [
  ["i8x16.replace_lane", 1],
  ["i16x8.replace_lane", 2],
  ["i32x4.replace_lane", 4],
];

const src = iota(16).map(i => (0x80 + i * 17) & 0xff);
const dst = iota(16).map(i => (0x40 + i * 29) & 0xff);

function readLane(bytes, size, lane, signed) {
  let v = 0;
  for (let i = size - 1; i >= 0; i--) {
    v = v * 256 + bytes[lane * size + i];
  }
  if (signed && v >= 2 ** (8 * size - 1)) {
    v -= 2 ** (8 * size);
  }
  return v;
}

function expected(base, dstSize, dstLane, value) {
  const out = base.slice();
  const low = BigInt.asUintN(8 * dstSize, BigInt(value));
  for (let i = 0; i < dstSize; i++) {
    out[dstLane * dstSize + i] = Number((low >> BigInt(8 * i)) & 0xffn);
  }
  return out;
}

for (const [extract, srcSize, signed] of extracts) {
  for (const [replace, dstSize] of replaces) {
    const funcs = [];
    const cases = [];
    for (let s = 0; s < 16 / srcSize; s++) {
      for (let d = 0; d < 16 / dstSize; d++) {
        for (const self of [false, true]) {
          const from = self ? 0 : 16;
          funcs.push(`
    (func (export "f${cases.length}")
      (v128.store (i32.const 32)
        (${replace} ${d} (v128.load (i32.const 0))
          (${extract} ${s} (v128.load (i32.const ${from}))))))`);
          cases.push([s, d, self]);
        }
      }
    }
    const ins = wasmEvalText(`(module (memory (export "mem") 1) ${funcs.join("")})`);
    const mem = new Uint8Array(ins.exports.mem.buffer);
    cases.forEach(([s, d, self], i) => {
      mem.set(dst, 0);
      mem.set(src, 16);
      ins.exports["f" + i]();
      const value = readLane(self ? dst : src, srcSize, s, signed);
      assertSame(Array.from(mem.subarray(32, 48)),
                 expected(dst, dstSize, d, value));
    });
  }
}
