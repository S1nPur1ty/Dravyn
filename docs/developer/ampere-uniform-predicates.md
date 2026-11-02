# Uniform predicate lowering

The native frontend represents UR0–UR27 in reserved Maxwell registers and packs
UP0–UP6 into R239 bits 6–0. Ordinary predicates remain separate guest state.

Non-extended UISETP supports signed/unsigned comparison followed by AND, OR or
XOR with a uniform predicate, including inversion and UPT. Save the ordinary
predicate slots used by the destination and accumulator, load the accumulator
from R239, perform the existing Maxwell comparison, merge only the destination
bit into R239, then restore the ordinary predicates. Read-before-write matters
when the destination is also the accumulator. Extended comparisons and a second
uniform predicate destination remain explicitly unsupported.

Predicated ULEA uses the existing uniform guard save/load/restore sequence. Its
immediate materialization and arithmetic share the guard, so a false guard
preserves the destination. UPT maps to Maxwell PT; !UPT must not underflow the
uniform predicate slot calculation.

FSETP with a uniform second operand uses the existing operand materializer and
floating comparison lowering. It must also mark the program as using the
reserved uniform bank so native GPR accesses cannot alias that state.

Encoding reference: [Mesa NAK SM70 encoder](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/b1fe47e94409d81924c8663f58fac97e66ab4527/src/nouveau/compiler/nak/sm70_encode.rs),
`OpISetP`, `OpLea` and `OpFSetP`.

Regression checks live in `src/tests/video_core/ampere_uniform.cpp`. The hidden
`[ampere-capture-audit]` check reads `.bin` programs from
`EDEN_TEST_NATIVE_SHADER_DIR` without executing a game. It checks capture and
lowering, not runtime descriptors or visual correctness.

The 2.0.27 change adds support for four previously rejected captured fragment
programs. All 307 previously supported programs in the same capture corpus
produce byte-identical lowered code and address maps. The pipeline schema and
MoltenVK binary are unchanged; existing validated caches can be copied into the
new profile. Newly supported programs still need their first compilation.

## Indirect branch analysis

The UISETP.LT.U32 / USEL pair used to clamp a jump-table index must retain
the unsigned-minimum form recognized by Maxwell's indirect-branch tracker.
Recognize matching source, bound and predicate for all UP0–UP6 slots, not just
UP0; do not fold inverted selections or different comparisons. The comparison
remains in the instruction stream so its uniform predicate result is preserved.

The 2.0.27 crash reached this pair with UP1. Regression tests now continue through
control-flow analysis and SPIR-V generation, and captured-program audits inspect
every lowered BRX/JMX table. Graphics translation error reporting dumps the
captured code directly: rebuilding a failed CFG inside its exception handler
would throw again and terminate the GPU thread before logging the original error.
