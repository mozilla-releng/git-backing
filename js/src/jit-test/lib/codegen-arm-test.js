// Scaffolding for testing arm(32) Ion code generation patterns.  See
// codegen-x64-test.js in this directory for more information.

load(libdir + "codegen-test-common.js");

const arm_arch = {
    name: "arm",

    // End of prologue
    prefix: `
str lr, \\[sp, #-4\\]!
str fp, \\[sp, #-4\\]!
mov fp, sp
cmp r6, #35
bne \\+8 -> 0x${HEX}+
b \\+24 -> 0x${HEX}+
constant pool begin \\(length 0\\) \\[undefined insn\\]
nop
str lr, \\[sp, #-4\\]!
str fp, \\[sp, #-4\\]!
mov fp, sp(
str r9, \\[fp, #\\+8\\])?
`,

    // Start of epilogue
    suffix: `
ldr fp, \\[sp\\], #\\+4
ldr pc, \\[sp\\], #\\+4
`,

    // Instruction encoding
    encoding: `${HEX}{8}`,
};

// For when nothing else applies: `module_text` is the complete source text of
// the module, `export_name` is the name of the function to be tested,
// `expected` is the non-preprocessed pattern, and options is an options bag,
// described in codegen-x64-test.js.
function codegenTestARM_adhoc(module_text, export_name, expected, options = {}) {
    codegenTestShared_adhoc(arm_arch, module_text, export_name, expected, options);
}
