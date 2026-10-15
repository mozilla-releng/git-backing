// |jit-test| skip-if: !wasmSimdEnabled() || !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("loong64"); include:codegen-loong64-test.js

// A replace_lane fed by an extract_lane of at least its lane size moves the
// lane directly instead of through a general register.

for (const [replace, extract, expected] of [
  ["i8x16.replace_lane 15", "i32x4.extract_lane 3",   "vextrins\\.b \\$vr0, \\$vr1, 0xfc"],
  ["i8x16.replace_lane 2",  "i16x8.extract_lane_s 5", "vextrins\\.b \\$vr0, \\$vr1, 0x2a"],
  ["i8x16.replace_lane 7",  "i8x16.extract_lane_u 9", "vextrins\\.b \\$vr0, \\$vr1, 0x79"],
  ["i16x8.replace_lane 1",  "i32x4.extract_lane 2",   "vextrins\\.h \\$vr0, \\$vr1, 0x14"],
  ["i32x4.replace_lane 0",  "i32x4.extract_lane 3",   "vextrins\\.w \\$vr0, \\$vr1, 0x3" ],
]) {
  codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param v128 v128) (result v128)
         (${replace} (local.get 0) (${extract} (local.get 1)))))`,
    "f",
    expected);
}
