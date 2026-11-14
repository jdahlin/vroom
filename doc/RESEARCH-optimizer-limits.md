# GCC/LLVM optimization limits and what VIR should borrow

2026-11-13. Research and inventory, not a change to compiler defaults.

## Decision

Borrow the structure of the policies: net inline cost, cumulative growth guards,
frequency-weighted allocation decisions, target-specific branch/vector costs,
and bounded analysis with conservative fallback. Do not bulk-copy the numbers.
No numeric default is approved for import by this review alone.

Existing programs depend on language semantics, ABI, extensions and implementation
limits. They benefit from established optimization heuristics, but do not acquire
a correctness contract with an inline threshold or spill weight. Performance
compatibility is still valuable: established defaults are useful experimental
priors when the algorithms, units and target conditions match.

## Scope and reproducibility

- [inventory.csv](optimizer-limits/inventory.csv): **1,126 rows**, every captured
  GCC parameter and LLVM numeric-option candidate, with current values, family,
  disposition and rationale. A disposition is a family-level recommendation, not
  a claim that every option was individually performance-tested.
- [metadata.json](optimizer-limits/metadata.json): versions, counts and scope.
- [sources.json](optimizer-limits/sources.json): 20 reference source files,
  pinned URLs and SHA256 hashes. The inventory locates 398 option names in this
  source subset. A source location is a lookup aid, not proof of activation.
- [../bench/optimizer-limits.py](../bench/optimizer-limits.py): repeatable capture
  of compiler-reported options. It does not overwrite the reviewed CSV.

The installed references are GCC 15.2.0, x86_64-linux-gnu, and Ubuntu LLVM
23.1.6 (`clang --version` identifies revision `c47d7823685a`). GCC sources are
pinned to `releases/gcc-15.2.0`; installed effective values take precedence over
source initializers, including any distribution/target changes. LLVM source is
pinned to the installed revision, not the moving doxygen pages.

```sh
python3 bench/optimizer-limits.py --gcc gcc \
  --opt /usr/lib/llvm-23/bin/opt --out work/optimizer-limits-capture
```

The complete captured set is **305 GCC parameters** at O0/O1/O2/O3/Os and **821
LLVM numeric-option candidates**, including long integers, custom numeric
metavariables, numeric lists and scalable vector widths. The earlier rough
count of 779 LLVM options missed long/custom types and accidentally included
one nested `name=<int>` mapping; it was not an exhaustive count.

Seven LLVM list options have no entry in `--print-all-options`. Their value
cells remain empty; this does not mean zero. Zero, -1, UINT_MAX and UINT64_MAX
often mean automatic, unspecified or unlimited. The CSV records them literally.

This is an inventory-wide triage of those exposed controls, plus a source-backed
review of the relevant policy families below. It is **not** an enumeration of
every numeric literal in either compiler, every boolean/enum LLVM flag, every
target's effective defaults, or every change made by the Clang driver and pass
constructors. Examples of unexposed constants are examined explicitly below.
No claim of improved runtime or correctness coverage follows from this research.

## Inventory-wide triage

Each row has exactly one primary family; some settings have secondary roles.
Counts describe this installed build, not compiler complexity or policy quality.

| Family | Rows | Treatment |
|---|---:|---|
| Inlining | 56 | Adapt cost/growth policy; recalibrate units |
| Register allocation | 28 | Adapt cost/search policy to the sweep allocator |
| CFG, branches and layout | 106 | Account for frequency, growth and target costs |
| Loop transformations | 85 | Separate profitability from exploration limits |
| Vectorization | 49 | Model cleanup, checks, pressure and extraction costs |
| Memory optimization | 57 | Preserve legal proofs and bounded work |
| Scalar combining | 59 | Port valid patterns and bounded search, not raw budgets |
| Scalar analysis | 35 | Retain conservative bailout and local work budgets |
| Applicable target policy | 62 | Calibrate per CPU; inspect target overrides |
| Builtins/prefetch | 13 | Calibrate library/ISA/cache tradeoffs |
| Profile/frequency | 80 | Requires consistent frequency/profile semantics |
| LTO/IPA | 65 | Most require an absent or different interprocedural pass |
| Scheduling | 62 | Most require a different scheduler/DAG/pipeline model |
| Other targets/pipelines | 187 | Defer; includes GPUs, other CPUs, Polly and OpenMP |
| Diagnostics/testing | 66 | Exclude from normal speed tuning |
| Sanitizers/hardening instrumentation | 68 | Preserve instrumentation requirements |
| Runtime/ABI contracts and input limits | 20 | Derive from the actual contract |
| Compiler infrastructure/resources | 12 | Size for vcc's cold-build objective |
| Other runtime/specialized subsystems | 11 | Defer until there is a matching subsystem |
| Allocation training score | 5 | Diagnostic/training metric, not allocation policy |

An option appearing in help does not establish that its pass runs at O2.
Likewise a forced vector width or debug search limit is not evidence of a
production default. The seven GCC parameters that change between the captured
optimization levels are retained separately in the CSV; pass activation changes
much more than those seven numbers.

## Inlining: adopt growth accounting before importing thresholds

GCC O2 reports automatic/explicit-inline size limits 15/70, a called-once
limit of 4000, recursive depth 8, and unit growth 40%. O3 changes the first
pair to 30/200 and hint scaling from 200% to 600%. Its large-function threshold
is 2700 estimated instructions, with a 100% growth guard; large-unit threshold
10000; large-stack threshold 256 bytes with a separate 1000% growth guard.
These are separate safeguards, not one universal maximum function size.

LLVM's registered ordinary/hint/cold/hot thresholds are 225/325/45/3000.
The base instruction cost is 5 and call penalty 25; simplification, removed
call costs, attributes, target hooks and bonuses affect the net cost. Thus even
225/5 is not a rule to inline every function with 45 instructions. Source-only
defaults include O3/Os/Oz thresholds 250/50/5, loop penalty 25 and coldcc penalty
2000. A generic stack-size override reports UINT64_MAX, while recursive callers
still have a separate 1024-byte constraint.

VIR currently uses 150 AST nodes, a factor of four for used constant arguments,
and 4000 nodes for called-once bodies, with a large-caller restriction. These
numbers are not in either reference compiler's cost units. The shared value
4000 is not evidence that the policies are equivalent.

**Recommendation:** introduce measurable cumulative caller/TU growth accounting,
and estimate the surviving body under known call arguments. Preserve separate
legality, recursion, stack growth and cold-code decisions. Use GCC's growth
policy as a model, but choose the percentage only after defining whether vcc
measures AST, pre-IR, final VIR or emitted code. A conservative AST admission
check can remain as a cheap first stage. Do not replace 150 with 225 or 70.

Sources: [GCC parameter definitions](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/params.opt),
[LLVM inline cost](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Analysis/InlineCost.cpp),
[LLVM source-only constants](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/include/llvm/Analysis/InlineCost.h).
Vcc: `emit/emitimpl.h`, `emit/prepass.c`, `emit/walk/stmt.c`.

## Allocation: distinguish decisions from scoring and search protection

GCC exposes IRA loop register headroom 2, loop-count limit 100, and conflict
table budget 1000 MB. LRA has its own inheritance and reload-search controls.
These belong to its allocation algorithms; they are not VIR register counts
or function-size caps.

LLVM greedy has last-chance recoloring depth/interference limits 5/8 and a
10000-unit region-growth budget. VIR has neither the same interference search
nor that region-growth algorithm. Its first-use callee-saved cost reporting zero
does **not** mean saving a register is free: initialization consults target
costs, entry frequency and an 80% scaling factor.

An especially misleading apparent match is `regalloc-load-weight=4`,
`regalloc-store-weight=1`, `regalloc-copy-weight=0.2`, and cheap/expensive
rematerialization weights 0.2/1. These are **RegAllocScore**, a frequency-weighted
reward for ML training. They are not the normal greedy allocator's eviction
cost inputs. Importing them into VIR would create a new policy, not reproduce
LLVM's policy. Use such a score as an additional diagnostic at most.

The actual spill-weight code uses block frequencies, boosts apparent induction
updates by 3, hinted intervals by 1.01, and discounts rematerializable values by
0.5, then normalizes by interval properties and register-class factors. These
source-only constants illustrate why a CLI inventory is not the entire model.

**Recommendation:** first expose the dynamic costs of VIR's edge copies,
reloads, rematerialization and first use of callee-saved registers. Then test a
bounded frequency-aware policy for entry states and eviction. Retain existing
correctness checks and fallback. VIR's WEBMAX=4096, predecessor-voting bound 32,
and preload-candidate bound 16 constrain different work from LLVM's 5/8/10000.
Do not change them merely to match those figures.

Sources: [greedy allocation](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/CodeGen/RegAllocGreedy.cpp),
[spill weights](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/CodeGen/CalcSpillWeights.cpp),
[training score](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/CodeGen/RegAllocScore.cpp).
Vcc: `vir/alloc.c`, `arch/x64/emit/vir.c`, `emit/vir/a64.c`.

## Branches, duplication and switches: budget the emitted cost

GCC limits copied loop headers to 20 estimated instructions. LLVM's rotation
limit is 16 IR instructions; its generic tail-duplication limit is 2 machine
instructions, indirect-branch variant 20, and jump-threading threshold 6.
VIR permits small joins of up to two VIR instructions with duplication depth
below two, special empty/return paths below eight, and disables rotation plans
at 60000 blocks. Those constants protect different operations and recursion.

The useful transfer is a **cumulative emitted-cost bound** on duplication,
including allocator edits and edge moves. Copying two VIR instructions may
emit more than two machine instructions. Consider total function growth as
well as the local block size. Keep the already measured rotation benefit.

For jump tables, LLVM registers speed/size density thresholds 10%/40%, with
target hooks. GCC's `case-values-threshold=0` delegates to its target, and its
speed/size growth-ratio limits are 800%/300%. VIR x86 currently requires at
least six ranges, span below 4096, and at least 25% coverage. A 185-case dense
SQLite VM switch already passes this test: lowering the density threshold
cannot fix its dispatch sequence. Evaluate sparse switches separately before
changing this policy.

For if-conversion GCC distinguishes predictable/unpredictable cost ceilings
20/40. LLVM uses target costs and separate select/if-conversion policies.
VIR's eight-result and cheap-arm bounds do not account for every dependency
chain; the TSVC search-loop regression is evidence to model that distinction,
not a reason to adopt GCC's 20/40 in unrelated units.

Sources: [rotation](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/LoopRotation.cpp),
[jump threading](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/JumpThreading.cpp),
[block placement](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/CodeGen/MachineBlockPlacement.cpp),
[target lowering](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp).
Vcc: `vir/layout.c`, `emit/vir/build.c`, `arch/x64/emit/vir.c`.

## Memory/scalar analysis: transfer the budget contract, not its magnitude

GCC DSE allows 256 alias queries per store; SCCVN allows 1000 per access.
LLVM LICM caps certain MemorySSA work at 100, promotion accesses at 250;
EarlyCSE's cap is 500. DSE separately budgets scan/walk/path work at 150/90/50
and assigns same-block/other-block work different costs (1/5). GVN has its own
100-instruction and 100-dependence search limits.

VIR's far-load reuse scans at most 32 writers and follows bounded address
chains. A writer scan is not a MemorySSA clobber query. Raising 32 to 256 does
not create stronger alias information, and copying LICM's cap does not supply
loop preheaders or safe speculative motion.

**Recommendation:** count budget exhaustion, distinguish false opportunity
rejections from unavailable proofs, and measure actual work per query. Increase
only a demonstrably binding budget. On exhaustion keep the original operation;
never interpret an unfinished alias search as proof of independence. Apply the
same rule to value-tracking depth, SCEV complexity, PHI exploration, SROA and
combining limits. Preserve vcc's existing compile-time advantage.

Sources: [LICM](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/LICM.cpp),
[DSE](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/DeadStoreElimination.cpp),
[GVN](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/GVN.cpp).
Vcc: `emit/vir/build.c`, particularly `VW_FARMAX` and its invalidation rules.

## Loops and vectors: the work saved must exceed setup and pressure

LLVM registers `unroll-threshold=0`, but the implementation starts with 150
at ordinary optimization and 300 at O3, then applies target and explicit
overrides. Runtime unroll count 8 and unroll-and-jam inner threshold 60 are
additional source defaults. GCC uses independent caps: 200 unrolled
instructions, average 80, factor 8, complete-peel size 200/count 16. None is
a portable instruction budget for VIR.

LLVM's vectorizer minimum trip count 16 is conditional: smaller known-trip
loops can vectorize if scalar-iteration overhead is absent. It separately
limits memory checks to 128 and SCEV checks to 16. GCC bounds alias/alignment
versioning checks at 15/6. VIR's 256-node expression budgets and eight ordered
reductions are construction/search bounds, not a profitability model.

**Recommendation:** adopt explicit setup, tail, runtime-check, lane-extraction
and register-pressure costs before expanding vectorization acceptance. Treat
analysis budgets separately. Wider hardware is not justification to blindly
raise a limit; unsupported transformations remain deferred.

Sources: [unrolling](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/LoopUnrollPass.cpp),
[vectorization](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Vectorize/LoopVectorize.cpp).
Vcc: `emit/vir/vec.inc` and the search-loop findings in `PLAN-vir.md`.

## Frequency and target inputs: unify the meaning first

GCC's builtin-expect probability is 90%. LLVM lowers ordinary expect with
weights 2000:1 (roughly 99.95%). Both are established compilers; their different
values refute the idea of a single compatibility number. Profiles, static
heuristics, user hints and cold attributes should have a documented precedence.
Do not confuse percentile cutoffs, entry-relative frequency, probability and
absolute execution counts when transferring a policy.

Cache sizes, vector widths, issue rates, instruction costs and branch encodings
belong to the target model. LLVM's forced-register-count and forced-cost flags
are experiment controls, not realistic machine descriptions. Stack-clash probe
intervals and ABI-visible interference sizes are a different category again:
derive them from the target/runtime contract rather than runtime microbenchmarks.

Source: [expect lowering](https://github.com/llvm/llvm-project/blob/c47d7823685a/llvm/lib/Transforms/Scalar/LowerExpectIntrinsic.cpp).

## Proposed implementation order and acceptance evidence

1. **Instrument existing limits.** Record attempts, budget exhaustion, work,
   estimated growth, emitted growth and allocator edit kinds. Name/document the
   current constants and their units without changing behavior. This is the
   prerequisite to deciding which limit is actually restrictive.
2. **Inline growth accounting and simplified-body estimates.** This directly
   addresses the measured large-caller problem and generalizes beyond SQLite.
   Keep single-use and constant-argument cases distinct.
3. **Allocator entry/edge profitability.** Investigate inherited constant
   states and hot pointer reloads using the measured VM paths; test a general
   rule, not an opcode or function-name exception.
4. **Duplication/if-conversion/vector profitability.** Include emitted edge
   work, dependency chains and scalar overhead in each matching pass.
5. **Selective analysis-budget changes.** Only after exhaustion counters show
   missed worthwhile opportunities; defer knobs for absent passes.

For each candidate, compare a bounded neighborhood of values against the
current compiler, changing one policy at a time. Use a fixed development set
and an untouched holdout from SQLite, zlib, PostgreSQL, FFmpeg and TSVC. Report
per-workload runtime, compile instructions/time, code size and relevant dynamic
edit counts; do not let a geometric mean conceal a serious regression. Freeze
acceptance limits before measurements. Check ARM and x86 independently where
the shared rule affects both, and retain existing correctness/sanitizer gates.

This review adds no new performance requirement to CI and claims no release
gate passed. Native portfolio runs and threshold calibration belong to the
implementation experiments, not to this read-only reference inventory.

## Current-code follow-up: concrete work packets (2026-11-13)

This follow-up reviews the implementation, not just the reference thresholds.
The order below refines the earlier list: start with a small allocator attribution
experiment, then choose between allocator policy and inlining using its evidence.
No runtime improvement is established by this review.

### 1. Attribute allocator edits on x86 before changing their policy

`vir/alloc.h` already has `vir_astats`: intra-block reload/remat/move/store counts
and separate edge counts. Reuse these. `emit/vir/a64.c` already implements
`VCC_VL_PROBE` (now `-fvir-probe`); x86 needs equivalent attribution at its edit-emission sites, plus
an optional mapping from an edit to its block, value and entry-state decision.
Keep reports opt-in and function names as diagnostic filters only.

The measured SQLite VM has 165 million zero materializations near dispatch
edges and 92 million reloads of the aMem stack location. These are candidates,
not proof of dead code or allocator mistakes. Determine which are remats,
edge reconciliation, ordinary lowering, or loads that alias analysis must retain.
The existing instruction-category probe cannot answer that distinction.

Deliverable: dynamic edit counts linked to static allocator decisions. Verify
unchanged objects with diagnostics disabled and matching program results when
enabled. Nop probes require alignment-controlled probe/control binaries; use
unmodified binaries for runtime comparisons. Counts in VdbeExec remain exclusive
of out-of-line callees, so also report whole-program performance.

### 2. Price block-entry choices by the work they cause

`vir/alloc.c:enter` chooses an already-processed predecessor using live-in
register count, coldness, physical adjacency and a strongly dominant loop-depth
term. It inherits that predecessor's live-in register states, including
constants. This can make other incoming edges pay to recreate a cheap constant
while useful nonconstant values compete for registers. Whether this explains
the measured zeros remains to be established by packet 1.

First experiment: estimate reconciliation work for a bounded set of candidate
entry states, distinguishing register copies, reloads, stores and rematerialized
constants. Start with existing loop/cold information and deterministic tie breaks;
do not present those as measured execution frequencies. Compare a candidate with
the current choice before installing it. Include later/backedge reconciliation
in validation even though their register states are unavailable at this point.
Avoid a quadratic predecessor-by-live-value search on giant switch functions.

Keep victim selection separate: `victim` currently uses next-use distance with
a constant tie preference. Replacing that and entry selection simultaneously
would hide the cause of a result. LLVM's ML reward weights are not suitable
replacement costs. Use the allocator checker/sweeps and randomized cases, then
ARM/x86 correctness and native runtime tests. Success means lower useful dynamic
edge work and a runtime benefit, not merely fewer static moves.

### 3. Plan inline decisions once, with caller growth and specialization cost

`emit/prepass.c:fn_isize` already computes bottom-up expanded sizes and recursion
groups before worker emission. Preserve this deterministic design.
`inl_constarg` only establishes that a referenced parameter receives a constant;
it does not estimate the code remaining after specialization. The fourfold
allowance can therefore reward constants that remove almost no work. The
single-use rule tests original caller/callee sizes, not cumulative caller growth.

Add per-callsite decision records in the prepass, consumed by
`emit/walk/stmt.c:wk_can_inline`, so estimates and admission cannot drift apart.
First record original caller size, accumulated expansion, admission reason and
resulting emitted size. Then trial a caller-growth guard separately from a
bounded constant-substitution estimate. Estimate removable branches and work
without mutating the shared AST; cache only summaries with a valid argument
context. Keep always-inline semantics, recursion restrictions and target-feature
legality intact. Do not add a shared budget mutated by emission workers.

Transfer GCC/LLVM's distinction between call overhead, simplified body and
aggregate growth; retain vcc units until calibrated. Single-use removal of an
out-of-line body and growth of its caller are different costs. Check output
stability across worker counts and the inline regression suite. Evaluate both
large amalgamations and ordinary multi-file projects before choosing a limit.

### 4. Add small target cost hooks where profitability is currently structural

- **If-conversion:** `vw_armempty` permits up to two cheap instructions and
  `vw_ifconv` up to eight selects. Price the executed work on both arms and the
  select dependency chain; do not translate GCC's 20/40 directly into these
  counts. Measure predictable and unpredictable branches separately.
- **Vectorization:** `vec.inc` already reports rejection reasons and rejects
  ordered-reduction-only loops on ARM after a measured regression. Preserve
  that evidence while estimating setup, tail and serial lane extraction on
  each target. Vectorized-loop count is not an acceptance metric. Start with
  these supported forms before adding general unrolling or runtime versioning.
- **Layout/duplication:** include allocator reconciliation and final code size
  in duplication experiments. A locally shorter branch sequence can increase
  edge traffic or worsen instruction-cache behavior. Reuse packet 1's counts.

These should share documented target-cost units where meaningful, but do not
require a large universal cost-model framework before the first experiment.

### 5. Separate missing alias proofs from exhausted search budgets

`emit/vir/build.c:vw_unwritten` stops at 32 writers, but also stops immediately
at a non-store writer, an unproved base relationship, an excessive offset or an
overlapping store. Split those rejection counters first. Raising 32 cannot fix
unknown aliasing. If budget exhaustion is rare, investigate a narrowly justified
alias proof instead of a larger scan. Retain conservative fallback on every
unfinished proof.

Most current scalar work runs on builder IR in `vw_emit`, including a second
promotion/simplification round. Implement improvements at the phase with the
needed facts; do not assume the compiler lacks promotion or needs an entire
MemorySSA/LICM infrastructure to make progress.

### What should wait, and the gate for each experiment

Do not lead with another range-analysis or local-DSE expansion for SQLite.
The experiments recorded in `PLAN-vir.md` at commit `feaaed26` removed instructions
but regressed runtime: the range change by about 1.1–1.4%; the later DSE trial
removed 69 static instructions overall without reducing the measured instruction
ratio. They may help a different corpus, but need fresh evidence. Also defer
bulk threshold increases, copying greedy-allocator reward constants, and
function/opcode-name exceptions.

For each packet, freeze a baseline and change one decision policy. Run its
focused correctness checks, then short interleaved native comparisons on ARM
and x86 where available. Use SQLite plus non-SQLite development workloads from
zlib, PostgreSQL, FFmpeg and TSVC, reserving independent programs/input cases as
holdouts before tuning. Report runtime, emitted text size, compile time and
instructions, and the diagnostic quantity the policy intended to improve.
Record unavailable target/project runs rather than claiming portability from
QEMU timings. Lock regression tolerances and repetition rules before the sweep;
retain only changes whose benefit survives reruns and holdouts without violating
the compiler's compile-time budget. No new arbitrary tolerance is approved here.
