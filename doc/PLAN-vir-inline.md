# Inlining on VIR

Status: **proposal** (2026-11-27), nothing implemented.  Steps I0-I5 below.

## Today

Inlining happens while the walker reads the AST (`walk_call_can_inline`,
`walk_call_inline` in `codegen/walk/statement.cpp`): each copy is walked
again into the caller's pre-IR and the passes then work on the whole.  A
callee's size is its AST node count with what it inlines itself
(`codegen_inline_size`, `codegen/prepass.cpp`): 150, 600 with a constant
argument, 4000 for a static function called once.

clang inlines on IR, callees first: each function is optimized, then its cost
(instructions that remain, minus what the call's arguments would fold) is
weighed against a threshold (225; 325 `inline`; 250 at `-O3`; 3000 hot call
site, with a profile or block frequencies; 45 cold; +50% single block; up to
+150% vector code; 15000 for the last call of a static function; 5 per
instruction, 25 per call).

## What gcc and clang do

Read from their sources on 2026-11-27 (gcc master, llvm release/23.x); gcc's
badness formula and its instruction weights were not read.

**gcc** has two inliners.  An *early* one runs callees first, on already
optimized callees, with a tiny growth budget (`early-inlining-insns` 6, 14 at
`-O3`).  The *IPA* one works on per-function summaries and takes the best call
of the whole unit first (a heap keyed by "badness", roughly cost over benefit;
ties by id), then inlines functions called once.

- Limits depend on the `inline` keyword: `max-inline-insns-single` 70 (200 at
  `-O3`) for declared-inline or hot callees, `max-inline-insns-auto` 15 (30)
  for others.  A cold call that grows code is refused.
- A summary keeps size and time per pair of *predicates* on the parameters
  (when the code runs; when it survives specialization), so a constant argument
  shrinks the estimate without looking at the body again.
- Hints (known loop bounds or strides, indirect calls made direct) and an
  estimated speedup over `inline-min-speedup` (30%, 15%) raise the limits.
- Growth budgets: the unit 40% (`inline-unit-growth`), a large function 100%
  past 2700 instructions, the stack frame past 256 bytes.

**LLVM**'s main inliner walks the call graph's SCCs callees first and decides
each call alone:

- Cost is 5 per instruction that stays.  Folded constants, branches that die
  with the arguments, phis, most casts and loads from an argument that will
  become registers are free; if such an argument's slot turns out to escape,
  its savings are charged back.  The cost starts negative by what the call
  itself costs.  The walk stops as soon as the threshold is passed.
- The threshold is raised for `inline`, hot call sites and single-block or
  vector callees, and set low for cold call sites.
- The function clean-up passes run after each SCC's calls are inlined, not
  between two inlines.
- Recursion: no direct self-inlining, a history per copied call, and a cost
  multiplier for calls that stay inside an SCC.
- The splice hoists static allocas to the entry, saves the stack around
  dynamic ones, turns calls into invokes under a landing pad, merges returns
  into a phi, and gives debug locations an `inlinedAt`.

**Taken over here:** callees first; free-if-it-folds costing with an early
stop; a higher limit for `inline`; little room for calls that run seldom (the
walker now does this by loop depth); a per-function growth cap.  **Not:** a
unit-wide heap (it needs every function built before any is emitted) and
profile-based hotness.

## What there is to gain

Measured on this branch's base (x86-64 host, Stockfish x86-64-v2 `-O2`
`bench 16 1 13`, best of 3 runs of user cycles; SQLite amalgamation `-O2 -c`):

| vcc `WALK_INLINE_VIR_MAX` | Stockfish cycles | Stockfish text | SQLite compile instructions | SQLite text |
|---|---|---|---|---|
| 40 | 122.8 G | 971 K | 2.89 G | 1.29 M |
| 150 (today) | 12.5 G | 1091 K | 4.15 G | 1.78 M |
| 300 | 12.0 G | 1139 K | 5.36 G | 2.34 M |
| 600 | 12.4 G | 1196 K | 7.17 G | 3.04 M |

| clang `-inline-threshold` | Stockfish cycles | text |
|---|---|---|
| 0 | 8.46 G | 492 K |
| 225 (default) | 8.14 G | 507 K |
| 1000 | 8.35 G | 583 K |

(The clang builds are this plan's own, without `-fPIE`; compare rows within a
table.)

- Inlining small functions matters enormously, and vcc already does it.
- Past that, **a better inliner is not where Stockfish's 1.5x is**: clang at
  threshold 0 still beats vcc, and doubling vcc's limit gains 4%.
- Where the walker's inliner does cost is compile time and size: from 40 to
  150, SQLite's compile takes 43% more instructions and its text grows 38%.
  Each copy is walked, simplified and allocated again, and the AST count
  admits callees whose code is large.

So the goal of this plan is **the same or better speed with less code and
compile time**.  If the goal is Stockfish's speed alone, do I1 and stop.

## Design

A pass early in the pre-IR pipeline (after `dead-params`, before `unroll`)
replaces a call with a copy of the callee's pre-IR.

- **Order.** Callees first, over the recursion groups `codegen_inline_search`
  already computes (Tarjan).  No inlining within a group.
- **Callee form.** After a function's clean-up passes (through
  `simplify-again`), a candidate's pre-IR is saved as a relocatable snapshot:
  blocks, instructions, `extra` lists, slots, constants by value.  A splice
  renumbers values, blocks and slots and re-makes the constants (they are
  hash-consed per thread).  Symbol references are unit-wide already.
- **Threads.** Snapshots are immutable once published.  A caller that needs
  one not yet built builds it itself (as `codegen_function_walk_ensure` does
  for the prepass), so the work queue needs no dependency graph; the same
  function must give the same snapshot whichever thread builds it, which keeps
  output independent of `-j`.
- **Cost.** Instructions left in the snapshot, calls weighted, minus an
  estimate for constant arguments (instructions reachable only through the
  other side of a branch on them).  One threshold, a higher one for `inline`,
  a bonus for the last call of a static function; numbers set by I0's
  measurements, not copied from clang.  Loop depth of the call site stands in
  for hotness.
- **Stays in the walker.** `always_inline`, and whatever needs the argument as
  source: `__builtin_constant_p`, asm `i` operands (the kernel depends on
  both), `__builtin_object_size`.  Also the `-O1` direct emitter's inliner.
- **Not inlined at first.** Callees with `setjmp`, `alloca`/VLAs, inline asm
  with labels, `va_start`, computed goto, or other target features than the
  caller; a callee VIR cannot build stays a call.

### Hard parts

- **Exceptions**: a call inside the copy of a callee inlined at an `invoke`
  becomes an invoke of the caller's landing pad; the callee's own pads chain
  to it.
- **Debug information**: today's line rows come from the walker; the snapshot
  must carry each instruction's location (no inline records exist today).
- **Facts the walker takes from the call site** (parameter constants,
  address constants, unread arguments not evaluated): the simplifier has to
  recover each, or the copy is worse than today's.
- **Records and vectors** passed by value: the splice removes the ABI step,
  so these callees stop forcing a fallback only if the call is never lowered.

## Steps

| Step | Content | Done when |
|---|---|---|
| I0 | `bench/inline-measure.py`: the tables above for Stockfish, SQLite, zlib, PostgreSQL (speed, text, compile instructions) at a given policy | numbers reproduced |
| I1 (done: the loop-depth limit, `WALK_INLINE_VIR_OUTSIDE_LOOP_MAX`) | Tune the walker's policy: a limit for `inline` and one for others, loop depth of the call site | no benchmark slower, SQLite text not larger; a table in this file |
| I2 | Snapshot and splice for leaf, straight-line callees, behind `-fvir-passes`; verifier after every splice | suites green with `-fvir-verify -fvir-check`; output identical for every `-j` |
| I3 | Any control flow, loops, slots, calls (bottom-up) | I2's gate, `inline` suite |
| I4 | Cost model on snapshots; the walker keeps only the list above | speed no worse on every benchmark, SQLite text and compile instructions lower than today |
| I5 | Exceptions, debug rows | `vcc-eh`, `vcc-dwarf`, `cxx-run` |

Stop after I2 if the splice's compile cost is not clearly below the walker's
second walk of the same callee.

## Open questions

- Is compile time and size worth the work now, against Stockfish's other
  gaps (16-step loops not unrolled, spills in vector loops, `std::abs` as a
  library call)?
- Does the kernel build (arm64) depend on walk-time folding beyond the list
  above?  Not checked.
