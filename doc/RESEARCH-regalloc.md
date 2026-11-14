# Register allocation: state of the art, and what VIR should do

Research of 2026-11-11 for `PLAN-vir.md` step V4 and `SPEC-vir.md` section 15.
Sources: five web research passes (papers, source trees and their header
comments, design documents, maintainers' posts, mailing lists) and one pass
over `SPEC-vir.md` and `src/arch/x64/codegen/`. A claim carries the tag of
the page it was read on; a point that was only seen in a search snippet, or
that rests on a second-hand description, is marked *(unverified)*. Numbers are
the sources' own and were measured on their machines and corpora, not ours.
Each survey item ends with what VIR takes from it. Sections 6 and 7 are the
answer to the question and the design; a reader in a hurry can start there.

The question (Johan): can vcc improve on the register allocators in clang and
gcc by using more modern or advanced techniques?

Short answer: **on compile time, yes, by a large factor; on code quality, no,
not in general.** The published headroom between a good heuristic allocator
and an optimal one is a few percent, and LLVM and GCC have spent it in tuning.
What newer work gives is the same class of code from two linear sweeps
instead of an interference structure and a priority queue. Section 6 has the
long answer.

## 1. What GCC and LLVM do

### 1.1 GCC: IRA and LRA

- **IRA** is a regional graph-colouring allocator. The regions are the
  function and its natural loops, coloured top-down; the unit is an "allocno"
  (a pseudo's live range in one region). Colouring is Chaitin-Briggs with
  optimistic push; copy-related allocnos are grouped into "threads"; a hard
  register is chosen by lowest full cost including the cost imposed on
  conflicting allocnos [IRA].
- Live ranges are split **only on region (loop) borders**, with a later
  spill/restore motion step; finer splitting is left to LRA inside extended
  basic blocks [IRA] [LRA].
- **LRA** replaces reload. It loops until every instruction's constraints
  hold: create reload pseudos, inherit and split within an EBB, assign, spill,
  undo, rematerialize, substitute memory; a new stack slot changes
  displacements and forces another round [LRA]. Its author states that
  termination is not guaranteed (it aborts on too many passes) and that its
  assignment is a compromise because reusing IRA was too slow [LRA12].
- **Rematerialization** (`lra-remat.cc`) recomputes a spilled value when it
  has no memory access or division and its inputs are already in hard
  registers. The header says there is no cost model and that inputs are not
  kept live longer, for compile-time reasons [REMAT].
- **Fallbacks for large functions** are built in: above thresholds
  (`ira-simple-lra-insn-threshold`, a pseudos-times-blocks bound) IRA drops to
  one region, turns caller-saves off and stops building conflicts [IRA]
  [PARAMS]. `-fira-algorithm=priority` (Chow) exists as a faster alternative
  [GCCOPT].
- **Inline asm:** LRA picks the constraint alternative per instruction by cost,
  so `"rm"` can become a register [LRAC].

**VIR:** split only where it is cheap to reason about (block boundaries, calls,
fixed-register instructions); never iterate to a fixed point; have the
large-function fallback from day one; resolve asm alternatives in the
allocator as GCC does.

### 1.2 LLVM: Greedy, Fast, and the others

- **Greedy** (default when optimising since 3.0) replaced linear scan because
  linear scan spilled whole ranges and depended on a 2,600-line block-local
  rewriter to repair the result [OL11]. Design: live ranges in a priority
  queue, largest first; interference through one B+-tree of segments per
  physical register; a range that does not fit may evict cheaper ranges, is
  then split, and is spilled last [OL11].
- Stages in today's source: assign, evict, split (region, block, local),
  last-chance recolouring (bounded: depth 5, 8 interferences), spill
  [GREEDY]. Where to split a region is decided by a Hopfield network over
  "edge bundles" weighted by block frequency, iterated to a local minimum
  [SPILLP].
- Before allocation, PHI elimination and `TwoAddressInstructionPass` leave SSA
  and `RegisterCoalescer` joins copies aggressively; the allocator then sees
  non-SSA virtual registers and repairs with hints and recolouring [CG]
  [TWOADDR] [COAL].
- Olesen's numbers against the old linear scan: code size -1.2 % (i386),
  -1.6 % (x86-64), -2.3 % (armv7); the allocator pass itself 15-40 % slower on
  403.gcc, which was 1-3 % of `llc` time [OL11D]. The blog summary: 1-2 %
  smaller, up to 10 % faster [OL11]. Global splitting is about 40 % of the
  allocator's time [OL11S].
- **Fast** (`-O0`): block-local, bottom-up; every value that crosses a block
  boundary lives in a stack slot; no liveness dataflow [FAST]. **Basic** is a
  baseline; **PBQP** is 17 % slower to compile and 1 % slower code than Greedy
  on AArch64 by its maintainers' own slides, and is kept for irregular
  constraints [PBQP14].
- **MLGO:** a learned policy replaces Greedy's eviction heuristic. Reported
  gain: 0.3-1.5 % QPS on selected internal datacenter applications [MLGO21];
  in 2025, 0.5 % over a PGO+ThinLTO binary after about 7 days of training on
  100 machines [MLGO25]. A learned priority advisor is in tree with no
  published result [MLGOP].
- There is no paper on Greedy: the 2011 talk and a 2018 tutorial are the
  documentation [NOPAPER].

**VIR:** Greedy's order of preference (assign, then split, then spill) is
right; its machinery (interval trees, a queue, a network solver) is what costs.
The learned work shows how little is left to gain in eviction policy.

### 1.3 Compile-time share

- LLVM: register allocation is the second most expensive back-end phase after
  instruction selection; two-address rewriting and liveness are 25-45 % of the
  allocation-related time [E24]. Exact shares are in the paper's figures and
  were not read as numbers.
- Cranelift: allocation is the largest part of compile time; LLVM's allocator
  is 42 % faster than regalloc2; regalloc2 spends about 37 % of its time
  computing and merging live ranges [E24]. A regalloc2 maintainer puts
  allocation at about 30 % of Cranelift's time [RA3Z].
- GCC: on a 420K-line Fortran file at `-O3`, IRA about 9 % and combine plus
  reload another 9 % of GCC 4.6; LLVM 2.7's linear scan 7.2 % (Makarov, 2010)
  [MAK10]. A pathological `-O0` input: IRA 32 % and LRA 16 % [PR110587].
- Go: `regalloc` 12.6 % of compile time in a 2016 profile [GO14850].
- Single-file `-ftime-report` samples putting Greedy near 1 % of total clang
  time, and LLVM bug 24618 with 75 % in Greedy *(unverified)*.

**VIR:** allocation is where a fast back end spends its time, so its budget
must be stated per instruction like the builder's (7.9).

### 1.4 Where they are weak

- **Callee-saved registers and shrink-wrapping.** LLVM's shrink-wrap pass
  finds a single save and restore point and gives up on irreducible CFGs and
  multiple restore points [SHRINK]. Greedy's cost for the first use of a
  callee-saved register defaults to 0 [GREEDY]; a 2026 pull request that turns
  the cost on reports +1.21 % geomean on SPEC2017 rate on x86 (xz +9.8 %)
  [PR202007]. GCC had the mirror bug: IRA did not scale callee-save cost by
  entry frequency (PR111673; the fix gained 0.6 % SPECint and then regressed
  x86 in five PRs, ending in a target hook in 2025) [PR111673] [IRAHOOK].
- **Spill placement in loops.** Greedy placed spills around nested-loop
  induction variables; a pattern-matched heuristic in 2023 gave about 8 % on
  SPEC2017 exchange2 [PR72093].
- **Inline asm.** Clang turns `"rm"` into memory because the alternative is
  chosen during instruction selection, before allocation (issue #20571, open
  since 2014) [I20571]; the kernel works around it by narrowing constraints
  under clang [LT24]. Both compilers fail with out-of-register errors on
  constrained asm: LLVM reports and substitutes a filler register [RABASE],
  GCC has a history of "unable to find a register to spill" ICEs (PR104209,
  PR105032) [PR104209] [PR105032].
- **Two-address decisions before allocation.** LLVM commits commutation and
  LEA conversion in a pass that runs before interference is known [TWOADDR];
  reviewers have said the decision belongs in the allocator *(unverified)*.
- **Sub-register liveness** is opt-in per target and was turned off on RISC-V
  after miscompiles *(unverified)*.

**VIR:** these are the places a new allocator can be better by construction
(1.4 is the list section 6a draws on): asm alternatives, the callee-saved
decision made with the call positions in hand, two-address operands chosen
when the registers are known. VIR has no sub-registers (values are i32, i64,
f32, f64, ptr), which removes that bug class entirely.

## 2. SSA-based allocation

### 2.1 The theory

- The interference graph of a program in strict SSA form is **chordal**:
  dominance gives a perfect elimination order, and every clique is a program
  point. Colouring is then optimal in linear time, with exactly as many
  colours as the largest number of simultaneously live values, so spilling,
  colouring and coalescing separate [HGG06]. Found independently by Bouchez
  and by Brisk et al. [SSABOOK].
- Consequence (Hack's thesis): spill until at most k values are live
  everywhere; colouring then needs no further spill, and the
  build-colour-spill loop of Chaitin's family disappears [HACK07].
- Chaitin's NP-completeness proof is about colouring an arbitrary graph. With
  splitting allowed, "do k registers suffice" is not the hard part; the hard
  parts are critical edges, optimal spilling and optimal coalescing [BDR06].
  Most "spill everywhere" variants are NP-complete even under SSA [BDR07S];
  aggressive, conservative and optimistic coalescing are NP-complete, some
  even on chordal graphs [DARTE07].
- Puzzle solving (Pereira and Palsberg): with a parallel copy between every
  pair of instructions, spill-free assignment with pre-coloured and aliased
  registers is polynomial; spilling is Belady furthest-first. On LLVM 1.9, x86,
  SPEC2000: 9.6 % fewer static loads and stores than LLVM's linear scan,
  compile time within 3 % [PP08].

**VIR:** colour in dominance order (VIR's block array already is one, 2.3)
with no interference graph. Keep the two hard problems, spilling and move
removal, as separate small heuristics.

### 2.2 What breaks the theory in practice

- The SSA book's chapter lists what the theorem ignores: two-address and
  fixed-register operands, registers of different sizes and aliasing,
  operands that cannot be memory, and critical edges that cannot be split
  [SSABOOK]. Colouring with general register constraints is NP-complete even
  for one constrained instruction; the workable answer is to split every live
  range in front of a constrained instruction, which moves the cost to
  coalescing [HACK07].
- libFirm does exactly that: a `Perm` in front of a constrained instruction
  and a bipartite matching of values to admissible registers [FIRMCH]. Hack's
  thesis notes that x86 sub-register aliasing is not supported and that
  two-address code can need an extra register [HACK07].
- Cranelift's first allocator was decoupled and SSA-based (liveness,
  coalescing, spilling, reload, colouring in dominator-tree order) [CLOLD] and
  was replaced, first by regalloc.rs and then by regalloc2 (3.4).

**VIR:** a fixed-register instruction is handled locally, where it occurs, by
moving the few values in the way (7.4); no global pre-colouring.

### 2.3 Spilling: Belady on a CFG

Braun and Hack extend Belady's MIN (evict the value whose next use is
farthest) from a block to a CFG [BH09]:
- per block a set W of values in registers and S of values already spilled;
- next-use distances from a dataflow in which an edge leaving a loop is very
  long, so a use after the loop is farther than any use inside it;
- at a loop header, values used in the loop come first, and a value that is
  only live through the loop gets a register only if the loop's maximum
  pressure leaves room;
- spills and reloads go on the edges where W and S disagree.

Measured in libFirm on x86 with 7 registers (CINT2000): executed reloads down
54.5 % and spills down 61.5 % against LLVM's linear scan of the time, reloads
down 58.2 % against a Chaitin-Briggs allocator; spilling ran at about 430
instructions per millisecond on a 2 GHz Core 2 [BH09]. Furthest-first is not
optimal (a first spill costs a store and a load, a second only a load)
[SSABOOK].

**VIR:** this is the spilling policy (7.3), with next-use distances taken from
a use chain instead of a dataflow.

### 2.4 Moves: destruction, parallel copies, coalescing

- A parallel move is sequentialised with one temporary per cycle; the
  algorithm is verified in Coq and used in CompCert [RSL08]. Boissinot et al.
  give out-of-SSA translation through parallel copies that is correct in the
  presence of critical edges and about 2x faster than Sreedhar's method III
  [BOI09].
- **Preference-guided assignment** (Braun, Mallon, Hack) is biased colouring
  with no interference graph: a pass collects register preferences, then the
  dominance-order assignment follows them. 6.2 µs per instruction against
  14.1 µs for recolouring; executed moves as a share of instructions 13.5 %
  with no coalescing, 5.9 % preference-guided, 5.0 % recolouring; coalescing
  as such is worth 5 % run time, and the two methods are on par [BMH10].
- Tree scan with "repairing" is 6.97x faster than iterated register
  coalescing at comparable quality (abstract only) *(unverified)* [COL11].
- libFirm today: Belady spiller and chordal colouring by default, a
  recolouring copy heuristic, ILP variants, and the preference allocator as an
  alternative [FIRM]. Its authors report CINT2000 run times on par with GCC
  4.2.4 and LLVM 2.5 (a plot, not read as numbers) [BMH10]. No published
  compile-time comparison was found.

**VIR:** hints instead of a coalescing phase: block parameters and their
arguments, two-address results and their first operand, call arguments and
their ABI registers (7.5).

## 3. Linear scan and its descendants

### 3.1 The papers

- **Poletto and Sarkar:** one interval per variable, no holes, no splitting;
  spill the interval that ends last. Within about 10 % of colouring on most
  benchmarks (1.02-1.18, one outlier at 1.43); most of the time goes to
  liveness and interval construction, not the scan [PS99].
- **Traub et al., second-chance binpacking:** lifetime holes, splitting when a
  value is evicted, a second chance at its next use, and a resolution pass
  over CFG edges. Run time 1.00-1.09 of George-Appel colouring; without the
  second chance `wc` ran 38 % slower [TRAUB98].
- **Wimmer and Mössenböck (HotSpot C1):** holes, splitting at use positions,
  fixed intervals for register constraints (no reserved scratch), split
  positions moved out of loops, hints. C1 compiled 237 KB/s against the server
  compiler's 13 KB/s, whose colouring allocator took about half its time; the
  server compiler's code was 6 % faster at peak [WM05].
- **Wimmer and Franz, on SSA form:** intervals built in one backward pass with
  loops handled at the header and no iterative dataflow; phi resolution folded
  into edge resolution. Back-end time down 13-19 %, lifetime analysis 25-31 %
  faster, run time unchanged [WF10].

**VIR:** one backward pass for liveness (already 22.6 row 5), and the lesson
of Traub and Wimmer that whole-interval spilling is what makes plain linear
scan poor.

### 3.2 Go

Self-described as a linear scan variant: each block is run through a greedy
allocator that puts a value in a register just before its use and evicts the
value with the farthest next use; merge edges are fixed afterwards by a
"shuffle". Requirements: no critical edges; a block is scheduled after at
least one predecessor [GORA].

- One spill per value, placed by walking up the dominator tree from the
  restores to the first point where the value is still in a register [GORA].
- The start state at a merge is the end state of the visited predecessor with
  the fewest spilled live values, then by branch likelihood. Hints
  (`desiredState`) flow backwards from fixed-register inputs and two-operand
  instructions but not through phis (a TODO). A value whose arguments are only
  SP or SB is rematerialized [GORA24].
- Liveness with next-use distances (`computeLive`) iterates to a fixed point
  and its own comment says it could be quadratic [GORA24]. With `-N -l` it
  reached about 30K blocks with about 5K live values each and multi-GB memory
  [GO45897].
- An affinity graph to remove phi moves is still a TODO in the header, and an
  open issue has its maintainer saying it uses more copies than necessary in a
  hot loop [GORA] [GO16122].

**VIR:** the same shape of allocator, with three changes: liveness without a
fixed point (7.2), hints through block parameters (7.5), and a bound on the
live-set memory with a cheaper fallback (7.2).

### 3.3 V8 and JavaScriptCore

- **TurboFan/Turboshaft:** linear scan with splittable live ranges and bundles
  of phi-connected ranges as hints; Turboshaft kept this back end unchanged
  [V8RA] [V8TS]. An LLVM-style greedy allocator was tried and retired in 2016:
  results were "inconclusive" against the tuned linear scan [V8GREEDY]. A
  single-pass mid-tier allocator existed as a fallback for huge Wasm
  functions and was removed in 2023 [V8MID].
- **Maglev:** a prepass computes linear live ranges and next-use chains; one
  forward pass tracks register state and evicts by furthest next use; a
  spilled value has one slot, stored right after its definition; merges get
  gap moves and phis prefer their inputs' registers [MAGLEV]. Maglev compiles
  about 10x faster than TurboFan [MAGLEV].
- **JavaScriptCore Air:** iterated register coalescing straight from the
  paper (2016); LLVM's Greedy was faster as an allocator (time ratio about
  0.6) while LLVM as a whole took 4.7x longer [B3]. Air's Poletto-Sarkar
  linear scan served the lower tier; its own comment said `-O2` code ran
  1.5-2x faster than `-O1`, mostly from colouring [AIRGEN]. In February 2025
  WebKit added an LLVM-style greedy allocator, removed linear scan in January
  2026 and graph colouring in July 2026, stating faster compiles and better
  benchmark results without figures [AIRGREEDY] [AIRRM].

**VIR:** Maglev is the closest published relative of the recommended design.
JavaScriptCore is the warning about the fallback (7.10): for long-running
code a production engine found greedy splitting worth having.

### 3.4 Cranelift

- **regalloc2** is a backtracking allocator in the family of LLVM's Greedy:
  exact liveness by iterative dataflow, live ranges merged into bundles, a
  priority queue, a B-tree per physical register, eviction by spill weight,
  one split per iteration at the first conflict, a second chance before the
  stack. Operands carry constraints (any, fixed, reuse-input, stack); clobbers
  are one-point ranges on the register; block-parameter, edge and split moves
  are sorted "half-moves" resolved as parallel moves with a scratch register;
  critical edges must be split by the client [ION].
- Against its predecessor: about 20 % faster compiles overall and up to
  10-20 % faster generated code under register pressure. Lessons its author
  draws: contiguous inline data and bit-packing (a struct growing from 16 to
  24 bytes was measurable), precise liveness pays for itself, simple split
  heuristics beat cost models, and development was driven by fuzzing against
  a **symbolic checker** of the allocation rather than by hand-written tests
  [RA2].
- **fastalloc** (2024): reverse single pass, no liveness, every live-in and
  block parameter in its slot at block boundaries. Wasmtime: compile -23 %,
  a benchmark +67 % slower [FASTALLOC] [WT9611].
- **regalloc3** (d'Antras): closer to LLVM's Greedy, adds rematerialization
  and register groups; compile 1.06-1.29x faster than regalloc2 with run time
  between 3 % slower and 6 % faster [RA3] [RA3Z].
- Rematerialization is not in regalloc2; Cranelift does it for constants in
  the mid-end, its author agreeing the allocator would be the ideal place
  [CLREMAT].

**VIR:** the checker, the operand-constraint vocabulary (fixed, reuse-input,
clobber), the bounded probe for slot sharing, and the fallback design (7.10).

### 3.5 Single-pass and other fast allocators

- **TPDE:** one analysis pass (loop forest, a live range per value as first
  and last block in a layout with loops contiguous) and one code generation
  pass with greedy allocation; every value has a slot; at a branch to a
  multi-predecessor block all values without a fixed register are spilled.
  8-24x faster than LLVM `-O0` at about equal run time; on optimised IR, 1.54x
  slower code than LLVM `-O1` on x86-64 [TPDE].
- **LuaJIT:** reverse linear scan fused with backward code generation;
  a blended cost per register, hints for loop recurrences and calling
  conventions, constants rematerialized instead of spilled [LJ09] [LJASM].
- **QBE:** spiller and allocator separated "thanks to SSA"; the spiller
  limits the live set per point by loop-depth-weighted cost, the allocator
  assigns per block with hints and fixes edges with moves in new blocks
  [QBE] [QBESPILL] [QBEREGA].
- **Zig's x86-64 backend:** a register manager that takes the first free
  register and otherwise evicts the lowest-numbered one, no live intervals;
  aimed at debug builds [ZIGRM].
- **MIR:** priority-based linear scan with splitting; code at about 0.91 of
  `gcc -O2` on 15 benchmarks, compiling about 100x faster [MIR].
- **RyuJIT LSRA:** Wimmer-style linear scan; a genetic search over the order
  of its 17 register-selection heuristics found orders that did not transfer
  across platforms and were not adopted [LSRA] [LSRAGA].

**VIR:** TPDE shows the floor (what "no real allocation" costs: about 1.5x
against `-O1`), which is the gap V4 exists to close. LuaJIT's backward hints
are in 7.5.

## 4. Optimal, search-based and learned allocation

| Approach | Published result | Time | Practical for vcc |
|---|---|---|---|
| 0-1 ILP (Goodwin and Wilken 1996) | Near-optimal on SPEC92 *(unverified)* | "hundreds of seconds" for a large procedure, as reported by [AG01]; no feasible solution for most functions over 1000 instructions [KOES] | No |
| Optimal spilling by ILP, then coalescing (Appel and George 2001) | Pentium code 9.5 % faster than their colouring baseline [AG01] | Up to 30 s per cluster | No; the split "spill first, then assign" is the idea to keep |
| PBQP (Scholz and Eckstein; Hames and Scholz) | In LLVM: 1 % slower code than Greedy on AArch64 [PBQP14] | 17 % slower compile than Greedy | No |
| Unison (constraint programming, with scheduling) | Estimated 1.1-10 % faster, 1.3-3.8 % smaller than LLVM on Hexagon, ARM, MIPS [UNISON] | 15-minute limit per function; largest function solved optimally has 946 instructions | No; an offline oracle at most |
| Progressive allocation by network flow (Koes and Goldstein 2006) | Approaches optimal with time | About 100x linear scan's allocation time per iteration [KOES] | No |
| MLGO eviction advisor | 0.3-1.5 % QPS on top of Greedy [MLGO21] | Training: days on 100 machines [MLGO25] | No |
| RL4ReAl (reinforcement learning, CC 2023) | Near Greedy, slightly behind on the x86 average; only for functions of 120-500 graph vertices [RL4REAL] | Model inference per decision | No |
| LLM assignment checked by a solver (VeriLocc 2025) | 85-99 % valid allocations for GPU kernels; no comparison with Greedy [VERILOCC] | About 1 s per inference | No |
| Exact colouring by grammatical CFG decomposition (ASPLOS 2025) | Exact spill-free instances at scale *(unverified)* [CAI25] | — | No; theory |

No peer-reviewed result from 2022-2026 was found that beats LLVM's Greedy on
both quality and compile time. The learned results are below 1.5 % and sit on
top of Greedy. (Caveat: the literature search used title search; a paper
without "register allocation" in its title can have been missed.)

The one paper in this group that matters for a design is **"Register
Allocation Deconstructed"** (Koes and Goldstein 2009), which solves each
sub-problem optimally in isolation to see which one matters [KG09]:
- **Spilling** is of "paramount importance" for performance: a heuristic
  spiller on x86-32 gave 20 % more dynamic memory operations and 7.5 % more
  run time. On x86-64 the extra memory operations did not become significant
  time.
- **Move insertion (splitting):** turning it off entirely grows code by less
  than 0.25 %; most of its benefit comes from allowing moves at block
  boundaries only.
- **Coalescing:** a greedy aggressive coalescer is within 0.15 % of optimal
  code size, with negligible run-time difference.
- **Assignment** is separable for performance and matters for code size.

**VIR:** spend the effort on spilling; allow moves at block boundaries and at
constrained instructions; do coalescing by hints.

## 5. The surrounding problems

| Problem | What the sources say | VIR 1.0 |
|---|---|---|
| Splitting policy | LLVM splits before spilling and gains 1-2 % size, up to 10 % speed over whole-range spilling [OL11]; IRA splits at loop borders only [IRA]; block-boundary moves capture most of the benefit [KG09] | A value changes location at an eviction, a call, a fixed-register instruction or an edge. No interval-splitting heuristic |
| Spill placement | Go walks up the dominator tree to one store [GORA]; Maglev stores after the definition [MAGLEV]; C1's store elimination removed 90 % of spill stores [WM05] | One store after the definition, emitted only if the value is ever reloaded from memory (7.3). Sinking the store to a colder dominating block is deferred |
| Slot sharing | LLVM colours spill slots by weight after allocation [SSC]; GCC shares by default [GCCOPT]; regalloc2 shares by size class with a bounded probe [ION] | Shared by size class when the linear ranges are disjoint, bounded probe (7.3) |
| Rematerialization | Briggs, Cooper and Torczon 1992 *(unverified)*; LLVM requires operands available at the use [LRE]; GCC's has no cost model [REMAT]; Go: only values built from SP/SB [GORA24] | Constants only: `iconst`, `fconst`, `symaddr`, `slotaddr`, `labeladdr` are never stored, always recomputed (7.6) |
| Coalescing vs colourability | Aggressive coalescing before allocation (LLVM) with compile-time caps [COAL]; optimal variants NP-complete [DARTE07]; hints as good as recolouring at half the time [BMH10] | Hints only; a hint is never allowed to cause a spill |
| Callee-saved vs caller-saved, shrink-wrapping | Both compilers got the first-use cost wrong for years (1.4); GCC places prologue components separately on PowerPC and AArch64 [SWS16] [GCC7] | A value live across a call prefers a callee-saved register when the cost test in 7.5 says so; otherwise it is reloaded after the call. Shrink-wrapping is deferred; the allocator records the first block that uses each callee-saved register so that a later pass can place the saves |
| Fixed registers, two-address | LLVM rewrites to two-address before allocation [TWOADDR]; regalloc2 has a reuse-input constraint [ION]; C1 uses fixed intervals [WM05] | The target's table (`vir_regs.fixed`, X7) gives fixed operands, clobbers and "result in first operand"; the allocator applies them locally (7.4) |
| Register classes, aliasing | Generalised colouring for overlapping classes [SRH04]; LLVM's register units [REGUNITS] | Two disjoint classes, integer and float; no sub-registers in VIR |
| Inline asm | LLVM chooses the alternative before allocation (#20571) [I20571]; GCC in LRA [LRAC] | The allocator chooses: register if one is free without eviction, else memory, else evict (7.7) |
| Block-argument moves | Parallel moves with one temporary per cycle [RSL08]; critical edges split by the client in Go, regalloc2, TPDE [GORA] [ION] [TPDE] | Parallel moves per edge; an edge that needs moves and is critical gets a block at lowering (7.8) |

## Comparison table

Time and quality are as published, on each source's own corpus; rows are not
comparable with each other beyond their stated baselines.

| Allocator | Algorithm | IR form | Compile time as published | Quality as published | Used by |
|---|---|---|---|---|---|
| IRA + LRA | Regional Chaitin-Briggs, then iterative constraint satisfaction | Non-SSA RTL | IRA about 9 % of GCC on one large file [MAK10] | Reference | GCC |
| Greedy | Priority queue, eviction, region splitting, recolouring | Non-SSA MIR | Pass 15-40 % slower than linear scan [OL11D]; second-largest back-end phase [E24] | 1-2 % smaller, up to 10 % faster than linear scan [OL11] | LLVM, and since 2025 JavaScriptCore's own version |
| RegAllocFast | Block-local, values in slots across blocks | Non-SSA MIR | — | `-O0` | LLVM `-O0` |
| PBQP | Quadratic programming heuristic | Non-SSA MIR | 17 % slower than Greedy [PBQP14] | 1 % slower code | LLVM, opt-in |
| regalloc2 | Backtracking over bundles | SSA or not, block parameters | About 30 % of Cranelift [RA3Z]; LLVM's allocator 42 % faster [E24] | Up to 10-20 % faster code than its predecessor [RA2] | Cranelift |
| fastalloc | Reverse single pass, slots at block ends | Same input | Compile -23 % [WT9611] | +67 % run time on one benchmark | Cranelift, opt-in |
| Poletto-Sarkar linear scan | Whole intervals, spill furthest end | Any | Up to 600x faster than colouring on pathological inputs [PS99] | 1.02-1.18 of colouring | tcc, formerly Air `-O1` |
| C1 linear scan | Holes, splitting, fixed intervals; SSA variant | SSA LIR | 18x the server compiler's speed [WM05] | Server compiler 6 % faster at peak | HotSpot C1, Graal, V8, RyuJIT |
| Go | Greedy per block, farthest next use, shuffle on edges | SSA | 12.6 % of compile (2016) [GO14850] | Open issues about extra copies [GO16122] | Go |
| Maglev | Next-use chains, one forward pass | SSA graph | Maglev about 10x faster than TurboFan overall [MAGLEV] | Mid tier | V8 |
| libFirm chordal | Belady spilling, then dominance-order colouring, then recolouring | SSA | Spilling 430 instr/ms (2009 hardware) [BH09] | Reloads -54.5 % vs LLVM's linear scan; on par with GCC 4.2 and LLVM 2.5 [BH09] [BMH10] | libFirm (cparser) |
| Preference-guided | Biased dominance-order colouring | SSA | 6.2 µs per instruction [BMH10] | Moves 5.9 % of executed instructions vs 5.0 % for recolouring | libFirm, alternative |
| Puzzle solving | Elementary form, puzzle per program point | SSA-like | Within 3 % of linear scan [PP08] | 9.6 % fewer loads and stores than linear scan | Research |
| Air IRC | Iterated register coalescing | Non-SSA | LLVM Greedy takes 0.6 of its time [B3] | 1.5-2x over Air's linear scan [AIRGEN] | JavaScriptCore until 2026 |
| TPDE | Liveness as block ranges, greedy in one pass | SSA | 8-24x faster than LLVM `-O0` [TPDE] | 1.54x slower than LLVM `-O1` | Umbra, TPDE-LLVM |
| LuaJIT | Reverse linear scan while emitting | SSA trace | — | — | LuaJIT |
| Unison | Constraint programming | SSA with alternatives | Minutes per function [UNISON] | 1.1-10 % over LLVM (estimated) | Research |

## 6. The answer

**(a) Where a newer technique gives better code.** In a few specific places,
not across the board.

- *Inline asm alternatives.* Resolving `"rm"` and `"g"` in the allocator gives
  a register where clang gives memory (1.4). GCC already does this; vcc would
  match GCC and beat clang. The kernel is full of such operands.
- *The callee-saved decision.* Both compilers have a history of making it
  with too little information (1.4, a 1.2 % SPEC geomean still on the table in
  LLVM in 2026). An allocator that knows, when it places a value, where the
  next call is and how deep the loop is, can make that decision directly.
  This is a claim about a cleaner decision, not a measured win.
- *Spilling under pressure.* Belady-style spilling on SSA measured fewer
  executed reloads than the linear scan and Chaitin-Briggs allocators of 2009
  [BH09]. Against today's Greedy with region splitting there is no published
  comparison; expect parity at best. On x86-64 and arm64, with 14 and about 28
  allocatable integer registers, pressure is the exception, and [KG09] found
  that extra memory operations barely show in x86-64 run time.
- *No sub-register and no pre-allocation two-address bug classes,* by
  construction (1.4). That is robustness, not speed of the generated code.

**(b) Where it gives the same code much faster.** This is the real gain, and
it is large.

- SSA form removes the interference graph and the fixed-point liveness:
  liveness in one backward sweep [WF10], assignment in one forward sweep in
  dominance order [HGG06], coalescing by hints at less than half the cost of
  recolouring for the same result [BMH10].
- LLVM and Cranelift spend the largest or second-largest share of their back
  end in allocation (1.3). The designs in 3.2, 3.3 (Maglev) and 3.5 show the
  same job in two sweeps over flat arrays. For most functions, those with no
  register pressure, the output of such an allocator and of Greedy differ only
  in which register was chosen and in a few copies.
- The optimal and learned methods of section 4 are the opposite trade: 0.3-10 %
  better code for 10x to 10,000x the time. None is usable in vcc, and their
  small gains are the evidence that the heuristic allocators are already near
  the limit.

**(c) Where GCC and LLVM are ahead and will stay ahead for a while.**

- *Splitting driven by block frequency.* Greedy's region splitting keeps a
  value in a register through the hot part of its range and in memory through
  the cold part, using profile or estimated frequencies. A one-pass allocator
  approximates this with next-use distance and a loop rule. JavaScriptCore
  moved from colouring and linear scan to a greedy allocator for this reason
  (3.3); V8 tried and stayed with linear scan (3.3). The evidence is mixed,
  and the fallback in 7.10 exists because of it.
- *Fifteen years of special cases:* folding a reload into a memory operand,
  rematerializing through chains, hoisting spills out of loops, commuting and
  converting to three-address form with interference known, compact-encoding
  register preferences, hint recolouring after the fact. Each is worth a
  fraction of a percent, and together they are most of what "tuned" means.
- *Shrink-wrapping,* imperfect as it is in both (1.4), exists there and is
  deferred here.
- *Profile-guided allocation.* vcc has no profile input.

**In one sentence:** vcc should not expect to beat clang or gcc on the quality
of allocation; it can expect to get within a few percent of them on code that
is not under register pressure, at a small fraction of their allocation time,
and to be better in the handful of places listed under (a). The 1.51x gap in
static instructions that `PLAN-vir.md` starts from comes from having no
allocator at all, and any competent allocator closes most of it; the last few
percent are where the two big compilers' tuning lives.

## 7. Recommendation for VIR

### 7.1 The algorithm

**SSA allocation in two sweeps: a backward sweep that builds liveness and
next-use chains, and a forward sweep in block order that assigns registers
greedily, spills by farthest next use, and follows hints.** No interference
graph, no interval objects, no priority queue, no fixed point. It is the
allocator of Go and Maglev (3.2, 3.3) with the spilling policy of Braun and
Hack (2.3) and the hints of Braun, Mallon and Hack (2.4), arranged for VIR's
flat arrays.

Why this and not linear scan over intervals, which `SPEC-vir.md` 15.2 assumed:
- An interval allocator needs an object per value with a list of ranges, a
  sorted worklist and active/inactive sets. Every one of them is a structure
  VIR's section 22 tries not to have.
- Whole-interval spilling is known to be poor (3.1), so a linear scan needs
  splitting, and a split interval needs the same per-edge resolution as the
  per-block design, without being simpler.
- In SSA with blocks in dominance order, "walk the blocks and keep a
  register-to-value map" already is the optimal colouring procedure when no
  spill is needed (2.1). The common case costs a table lookup per operand.

### 7.2 Sweep 5: liveness and next use (backward, fused with `dce`)

Positions are instruction indices. The sweep visits instructions from last to
first and maintains a live set `W` as a sparse set (a dense list plus a
per-value index, validated by cross-check, never cleared).

Per value operand visited, for value `v` at position `p`:
- push one word on the **use stack**: the position of the next use of `v` in
  linear order (`nu[v]`, 0 for none), with the top bit set if `v` was not in
  `W` (this use is its last on this path: a **kill**); then `nu[v] = p`;
- insert `v` into `W`.

At the definition of `v`: remove it from `W`; `nu[v]` now is its first use
(0: dead, and `dce` drops the instruction); record `xcall[v]`, whether the
nearest later call in a block that is not `VB_COLD` lies before `v`'s last
linear use; apply the hint rules of 7.5.

At a block boundary:
- bottom of block `B`: `W` becomes the union of the saved live-in lists of
  `B`'s forward successors (when `B` falls into the block just finished and
  has no other successor, `W` is already right);
- top of block `B`: `W` is appended to the **live-in pool** and `B` records
  the offset and count;
- top of a loop header: every `v` in `W` gets `lend[v] = max(lend[v], end of
  the loop)`. A value that is live into a header is live through the whole
  loop, which is the rule of [WF10]; it replaces the fixed point. The forward
  sweep treats `v` as live at any position up to `lend[v]`, and ignores a kill
  bit there.

Loops are intervals of the block array: for every edge to an earlier or the
same block, the range from the target to the source, merged into a properly
nested forest when two ranges overlap partially. This is exact for reducible
control flow and conservative for irreducible control flow (computed `goto`,
jumps into loops): a value can then be listed as live in a block its
definition does not dominate. That is harmless, because such a value is never
read there; the edge code in 7.8 skips a move whose source does not exist.
`vir_canon` builds the forest with the dominators (22.5).

Because the forward sweep visits the same operands in the opposite order, it
**pops** the use stack: no index, no per-instruction side record, sequential
memory in both directions. Constants take part in the chain but not in `W`
(7.6).

Cost and bound. The use stack is one word per value operand. The live-in pool
is the sum of the live-in sizes, which is small for ordinary C but quadratic
in the worst case; Go hit this with 30K blocks times 5K live values
[GO45897]. The pool is a per-thread reservation. If a function would exceed
it, the sweep stops saving lists and the function is allocated in **linear
mode**: `v` is live at `p` exactly when `p` is at most the larger of its last
use and `lend[v]`. That is the liveness of TPDE and Maglev (3.3, 3.5): always
sound, it loses lifetime holes (a value is held through a branch arm that
does not use it), and it needs no memory per block.

### 7.3 Sweep 6: assignment and spilling (forward)

State: `regval[r]`, the value in each register; `reg[v]`, valid when
`regval[reg[v]] == v`; `slot[v]`, the spill slot or 0; `nu[v]`, kept current
by popping the use stack.

**At an instruction:**
1. Apply fixed-register requirements and clobbers (7.4).
2. For each value operand not in a register: take a free register of its
   class, or evict (below); emit a **reload** edit, or a rematerialization for
   a constant. An operand the target can take from memory, or as an immediate,
   needs no register (the target's table says which).
3. Pop the use stack for each operand; free the registers of killed values.
4. Choose the result's register: its hint if free (7.5), else any free
   register, else evict.

**Eviction** picks, among the registers of the class not used by this
instruction, the value whose next use is farthest. The distance of `v` at
position `p` is `nu[v] - p`, with two corrections that stand in for the
dataflow of [BH09]:
- if `nu[v]` is 0 or lies beyond the innermost loop around `p`, and `v` was
  defined or last used inside that loop, its next use is the loop's back edge;
- a use beyond the innermost loop gets a large constant added per loop left
  (the 100000 of [BH09]), so values that are only live through the loop go
  first.

Constants are evicted before anything else at equal distance (they cost no
store). Evicting `v` writes nothing: it marks `slot[v]` (allocating a slot on
first need) and clears the register.

**One store per value.** A value with `slot[v] != 0` is stored once, right
after its definition; lowering emits that store when it reaches the
definition, which is why allocation and lowering are separate sweeps. Because
a value has one definition, its slot is valid wherever the value is live, so
an eviction never needs a store and no dataflow is needed to know whether
memory is current (Traub's allocator needs one, 3.1). Block parameters are
stored at the top of their block.

**Slot sharing.** `v` may take an existing slot of its size class if the
slot's previous occupant's linear range ended before `v`'s definition (not
before the eviction: the store is at the definition). A bounded probe of the
free list, then a new slot, as regalloc2 (3.4).

**At a loop header,** before entering: if the loop's maximum pressure
(recorded per loop by sweep 5 as the largest `W` in the loop's blocks) plus
the number of values live through the loop without a use in it exceeds the
class's registers, enough of the live-through values are dropped from the
entry state. They are reloaded after the loop. This is the header rule of
[BH09], and it is what keeps reloads off back edges.

**At a call:** every value in a caller-saved register that is live after the
call is evicted (it has or gets a slot) and is reloaded at its next use; the
argument values are moved into their ABI registers as one parallel move
(`LO(callv)`, X4). A value hinted into a callee-saved register stays.

### 7.4 Fixed registers and two-address operations (x86-64)

`vir_regs.fixed(f, v, &fix)` (15.1, X7) reports per instruction: a required
register per operand (shift count in `rcx`, dividend in `rax`), the result's
register (`rax` or `rdx` for division), a clobber mask (`rdx` for division,
`rdi`/`rsi`/`rcx` for `rep movs`, the caller-saved set for a call), and
whether the result must be in the first operand's register.

- **Required operand register `R`:** if another live value sits in `R`, move
  it to a free register, or evict it if none is free; then copy the operand
  into `R`. The operand keeps its old register if it lives on.
- **Clobber:** a value in a clobbered register that lives past the instruction
  is moved to a free register outside the mask, or evicted.
- **Result in first operand** (most x86 integer and SSE operations): if the
  first operand dies here the result takes its register and no code is added.
  If the operation is commutative and the second operand dies, the allocator
  swaps them (it flags the instruction; lowering passes them in that order).
  Otherwise the result gets its own register and lowering emits the copy
  first (X7 step 2); the target may use a three-address form instead (`lea`
  for `add` and small shifts).
- Backward hints (7.5) make the first two cases rare: the shift count is
  computed in `rcx` to begin with when `rcx` is free over its range.

arm64 has three-address operations and almost no fixed registers outside
calls, so only the call rule applies there.

X7 leaves two integer registers and one float register unallocated as scratch
for the target's operations. That stays in 1.0 because the existing `LO`
operations take temporaries through `treg`. It costs x86-64 two of its 14
usable integer registers; removing it (the table reports how many temporaries
an operation needs and the allocator keeps that many free at that instruction
only, as C1 does with fixed intervals [WM05]) is the first thing to measure
after V4.

### 7.5 Hints

Collected in sweep 5, where every use is seen before the definition (the
order LuaJIT exploits [LJ09]). `hint[v]` is a register or another value whose
register `v` should share.

- `v` is an argument of a call in a register-class position: that ABI
  register, if `v` is not live after the call.
- `v` is returned: the return register.
- `v` is a fixed-register operand (7.4): that register.
- `v` is an argument of `br` for block parameter `q`: the value `q`. In
  sweep 6 the first argument assigned sets `q`'s tentative register, later
  arguments and `q` itself try it. For a loop header `q` is assigned first and
  the back-edge argument follows it. This is the phi affinity Go's header
  lists as a TODO (3.2).
- At the definition of `v` by a result-in-first-operand instruction, `hint[v]`
  is copied to the first operand if that operand has none (one step of
  backward propagation per instruction, so a chain computes in place).
- `xcall[v]`: prefer a callee-saved register when one is already in use by
  the function, or when `v` is used in a loop deeper than its definition, or
  crosses a call inside a loop; otherwise prefer a caller-saved register and
  accept the reload. The first use of a callee-saved register costs a save and
  a restore on every entry, which is the cost both big compilers got wrong
  (1.4); the thresholds are to be tuned in V4 with the counters of 7.9.

A hint is taken only if the register is free. It never causes an eviction.

### 7.6 Constants and rematerialization

The constant region's values (`iconst`, `fconst`, `symaddr`, `slotaddr`,
`labeladdr`) have no definition point. The target's table says per use
whether the constant is an immediate or an addressing-mode operand of that
instruction; such a use needs no register. Otherwise the constant is
materialized into a register at the use and stays there like any value: later
uses find it, eviction drops it without a store, and a "reload" recomputes
it. It is kept across an edge only into a block with one predecessor, and
into a loop header when its next use is inside the loop (so a loop does not
recompute a symbol address every iteration). Nothing else is rematerialized
in 1.0.

### 7.7 Inline assembly

After X6 an `asm` instruction's operands arrive as `em_asmop` records: class,
fixed register, tied output, whether memory is allowed, and the clobber mask.
The allocator treats it as an instruction with those constraints (7.4), with
this order per operand that allows both register and memory: a register if
the value is in one or one is free; else memory, if the value has or can get
a slot; else eviction. An early-clobber output takes a register that no input
of the instruction holds. If the operands cannot be satisfied with every
other value evicted, the function is rejected with the diagnostic GCC and
clang give. Until X6 only the empty barrier exists and none of this runs.

### 7.8 Block entry, edges and block arguments

- **Entry state.** A block with one predecessor starts from that
  predecessor's end state, minus values not live in. A block with several
  starts from the end state of one visited predecessor, chosen as Go does: the
  fewest live-in values without a register, then loop depth, then not
  `VB_COLD`, then the block just before it. End states are saved per block as
  a short list of (register, value) pairs in a pool.
- **Other edges** must produce that entry state: a move for a value in a
  different register, a reload for one that is only in its slot, nothing for
  one the entry state holds in its slot (its store happened at the
  definition). Block arguments join the same parallel move. A move whose
  source exists neither in a register nor in a slot is skipped (7.2).
- **`VB_ADDR` blocks** (targets of `ibr`, 8.4) start with every live-in value
  in its slot and no value in a register; their edges are never split and
  need no code.
- **Sequencing** uses the standard algorithm with one temporary per cycle
  [RSL08]: a free register, else `xchg` on x86-64, else the reserved scratch.
- **Where the code goes.** The allocator records the moves per edge. If the
  source block ends in `br`, they go before it (the only case with block
  arguments, 8.2). If the target has one predecessor, they go at its top.
  Otherwise the edge is critical, and lowering emits a block for it: the
  branch targets a new label, the moves follow, then a jump. VIR's canonical
  form does not change, and an edge without moves costs nothing. A
  conditional back edge is the common critical edge, and the header rule
  (7.3) and the parameter hints (7.5) are there to leave it empty.

### 7.9 Output, checking, budget and measurement

**Output** (the function is not changed): `slot[v]`; one byte per value
operand and result in visiting order, the register at that instruction, read
sequentially by lowering; an **edit stream** in position order (reload,
rematerialize, move, swap-operands flag); per edge its parallel move; the set
of callee-saved registers used, each with the first block that uses it.

**Data structures**, all in the thread's reservations (22.2), none cleared
(22.3):

| Array | Size | Written by |
|---|---|---|
| `nu[v]`, `lend[v]`, `hint[v]` | 3 words per value | Sweep 5; `nu` advanced by sweep 6 |
| `xcall`, use count, class | 1 byte per value | Sweep 5 |
| Use stack | 1 word per value operand | Pushed by 5, popped by 6 |
| Live-in pool, per-block offset and count | Sum of live-in sizes | Sweep 5; absent in linear mode |
| Loop forest | 4 words per loop | `vir_canon` |
| `W` | Sparse set over values | Sweep 5 |
| `reg[v]`, `slot[v]`, `regval[r]` | 1 byte, 1 word per value; 1 word per register | Sweep 6 |
| End-state pool | 1 pair per occupied register per block | Sweep 6 |
| Operand registers, edit stream, edge moves | 1 byte per operand; 12 bytes per edit | Sweep 6 |

**Sweeps.** The count in 22.1 stays at 7. Liveness is fused with `dce` in
sweep 5 as before; sweep 6 is the allocation; sweep 7 the lowering. Sweep 5
does more per instruction than draft 0.3 assumed (the use stack and the
live-in lists instead of interval ends).

**Checker.** `vir_alloc_check` walks the function forward with the
allocator's output and tracks symbolically which value each register and slot
holds; every operand must read its own value, on every edge. This is
regalloc2's checker, which its author credits for the allocator's correctness
[RA2]. It runs after allocation in test and sanitizer builds and under
`-fvir-check`, and `fuzz-vir` drives it with random functions and with a
random register count (2 to all), so spill paths are exercised on small
inputs.

**Budget (proposal, to be replaced by measurement in V4):** sweeps 5 and 6
together at most 60 ns per VIR instruction, twice the builder's budget
(22.1); zero allocator calls to `malloc`.

**What to measure, with which harness:**
- *Compile time:* `bench/vbench.py ab emit` at `-O2` (VIR), and
  `-fvir-stats` for time per sweep; `vbench.py gate` without VIR (`-O1`)
  stays within 0.1 % (22.8).
- *Static quality:* instructions on the fixed TU set of V4 against today's
  `-O2`, against `clang -O2` and against `gcc -O2`, the ratio `PLAN-vir.md`
  already tracks (1.51x today on arm64).
- *Allocator counters* from `-fvir-stats`, per corpus, each also weighted
  by 10 to the power of loop depth: spill stores, reloads,
  rematerializations, register moves, edge moves, edge blocks created,
  callee-saved registers per function, functions that fell back to linear
  mode. These are what a change to a heuristic is judged by.
- *The size of the prize:* the same TU set through `clang -O2` with
  `-mllvm -regalloc=basic` and with the default. The difference in static
  instructions and in run time between LLVM's simplest and best allocators
  bounds what allocator quality alone can buy, and tells whether the fallback
  in 7.10 is worth building.
- *Run time:* the stage-2 self-build (`PLAN-vir.md` V7), PostgreSQL's
  `meson test` wall time (`bench/pg/build.sh vcc`), and the C suites as
  correctness at `-O2` (`bench/vcc-ctests.py`, VIR by default).
- *Lower bound:* V2's lowering with every value in a slot is the A side of
  every A/B.

### 7.10 The fallback

If, at V7, the counters show that the quality gate fails because of
allocation under pressure (reloads in loops, moves on back edges), the
alternative is a **backtracking allocator over live ranges in the style of
regalloc2** (3.4) behind the same interface: ranges from the same sweep 5,
bundles joined by the same hints, a priority queue, eviction by spill weight,
splitting at the first conflict. It is the documented design closest to
Greedy at a published cost of about 30 % of a fast compiler's time, and its
input and output (operand constraints, edits, per-edge moves) are the ones
7.4 and 7.9 already define. It would run only for functions where sweep 6
spilled inside a loop, so the common case keeps its cost.

The alternative in the other direction, if sweep 5's live-in lists prove too
slow, is linear mode (7.2) for every function.

### 7.11 Deferred past 1.0

Shrink-wrapping; sinking a spill store to a colder dominating block; folding
a reload into a memory operand of the using instruction; rematerializing
anything but constants; allocating the X7 scratch registers; profile or
branch-probability weights beyond loop depth and `VB_COLD`; vector and wide
values (not in VIR 1.0 at all).

### 7.12 What changes in `SPEC-vir.md`

- **15.2**, the sentence beginning "Algorithm: linear scan over SSA live
  intervals in block order" is replaced: the algorithm is the one in 7.1-7.8,
  and the output is per operand, not per value (a value can be in different
  registers at different uses).
- **15.2**, "per `br` a sequence of parallel moves" becomes "per edge": the
  edges that conform a non-chosen predecessor to a block's entry state need
  moves too, and they are not all `br`.
- **15.2**, "values live across a call prefer callee-saved registers" becomes
  the cost test of 7.5.
- **15.3** gains the edit stream, the store after a spilled definition, and
  the block for a critical edge with moves.
- **22.6** rows 5 and 6: "builds the allocator's live intervals" becomes
  next-use chains and live-in lists; "Forward over intervals" becomes forward
  over blocks. The count of 7 in **22.1** is unchanged.
- **8.2**'s parenthesis ("the moves for block arguments always go at the end
  of a block with one successor") stays true for block arguments; it is not a
  statement about all edge moves.
- X7's reserved scratch registers stay (7.4).

## 8. Uncertainties

- **No measurement of our own.** Every number above is someone else's, on
  another compiler and corpus, several from 2005-2011 hardware and from
  allocators (LLVM's linear scan) that no longer exist. The 60 ns budget and
  every threshold in 7.3 and 7.5 are proposals.
- **Quality under pressure against today's Greedy is unknown.** [BH09]
  compared against 2009 allocators. The fallback (7.10) is the hedge.
- **The liveness rule on irreducible control flow** (7.2) is argued here, not
  taken from a source; the checker and `fuzz-vir` must cover computed `goto`
  before it is trusted.
- **The loop corrections to next-use distance** (7.3) replace a dataflow with
  two comparisons. Whether that loses anything measurable against [BH09]'s
  dataflow is for the counters to show.
- **Sources not read in full:** Goodwin and Wilken, Hames and Scholz, Briggs
  et al. 1992, George and Appel, Chow 1988, Lupo and Wilken, Colombet et al.,
  Hack and Goos 2008, and the CGO 2007 coalescing paper (slides only). The
  compile-time shares in [E24] are in figures and were read only as ranking.
  GCC Bugzilla refused automated fetches; PRs are cited through mailing-list
  mirrors.

## Sources

- [IRA] https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/ira.cc
- [LRA] https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/lra.cc
- [LRA12] https://gcc.gnu.org/pipermail/gcc-patches/2012-October/348894.html
- [LRAC] https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/lra-constraints.cc
- [REMAT] https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/lra-remat.cc
- [PARAMS] https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/params.opt
- [GCCOPT] https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- [MAK10] https://gcc.gnu.org/pipermail/gcc/2010-May/191981.html
- [PR110587] https://gcc.gnu.org/pipermail/gcc-bugs/2023-August/832138.html
- [PR111673] https://gcc.gnu.org/pipermail/gcc-patches/2023-October/632089.html
- [IRAHOOK] https://www.mail-archive.com/gcc-patches@gcc.gnu.org/msg363264.html
- [PR104209] https://gcc.gnu.org/pipermail/gcc-bugs/2022-January/774826.html
- [PR105032] https://lkml.iu.edu/hypermail/linux/kernel/2203.2/07604.html
- [SWS16] https://gcc.gnu.org/pipermail/gcc-patches/2016-June/450173.html
- [GCC7] https://gcc.gnu.org/gcc-7/changes.html
- [OL11] https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html
- [OL11D] https://discourse.llvm.org/t/greedy-register-allocation/19339
- [OL11S] https://llvm.org/devmtg/2011-11/Olesen_RegisterAllocation.pdf
- [GREEDY] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/RegAllocGreedy.cpp
- [SPILLP] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/SpillPlacement.cpp
- [FAST] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/RegAllocFast.cpp
- [RABASE] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/RegAllocBase.cpp
- [CG] https://llvm.org/doc/CodeGenerator.html
- [TWOADDR] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/TwoAddressInstructionPass.cpp
- [COAL] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/RegisterCoalescer.cpp
- [LRE] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/LiveRangeEdit.cpp
- [SSC] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/StackSlotColoring.cpp
- [SHRINK] https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/CodeGen/ShrinkWrap.cpp
- [REGUNITS] https://llvm.org/devmtg/2016-11/Slides/Braun-DealingWithRegisterHierarchies.pdf
- [NOPAPER] https://lists.llvm.org/pipermail/llvm-dev/2022-January/154647.html
- [PBQP14] https://llvm.org/devmtg/2014-10/Slides/PBQP-update-and-in-the-wild.pdf
- [MLGO21] https://lists.llvm.org/pipermail/llvm-dev/2021-November/153639.html
- [MLGO25] https://www.llvm.org/devmtg/2025-04/slides/quick_talk/grossman_accurate_runtime.pdf
- [MLGOP] https://reviews.llvm.org/D133616
- [PR202007] https://github.com/llvm/llvm-project/pull/202007
- [PR72093] https://github.com/llvm/llvm-project/pull/72093
- [I20571] https://github.com/llvm/llvm-project/issues/20571
- [LT24] https://android-kvm.googlesource.com/linux/+/dbaaabd60e1662d2659eaeab0a4fc521667737ed
- [E24] Engelke and Schwarz, "Compile-Time Analysis of Compiler Frameworks for Query Compilation", CGO 2024: https://home.cit.tum.de/~engelke/pubs/2403-cgo.pdf
- [TPDE] https://arxiv.org/abs/2505.22610
- [HGG06] Hack, Grund, Goos, "Register Allocation for Programs in SSA-Form", CC 2006: https://compilers.cs.uni-saarland.de/papers/ssara.pdf
- [HACK07] Hack, "Register Allocation for Programs in SSA Form", thesis 2007: https://publikationen.bibliothek.kit.edu/1000007166/6532
- [BDR06] Bouchez, Darte, Rastello, WDDD 2006: https://pharm.ece.wisc.edu/wddd/2006/papers/wddd_05.pdf
- [BDR07S] Bouchez, Darte, Rastello, "On the Complexity of Spill Everywhere under SSA Form": https://arxiv.org/pdf/0710.3642
- [DARTE07] Darte's slides on coalescing complexity: https://perso.ens-lyon.fr/alain.darte/DEA07/regalloc.pdf
- [PP08] Pereira and Palsberg, "Register Allocation by Puzzle Solving", PLDI 2008: https://web.cs.ucla.edu/~palsberg/paper/PereiraPalsberg08.pdf
- [SSABOOK] "SSA-based Compiler Design", register allocation chapter: https://pfalcon.github.io/ssabook/latest/book-full.pdf
- [BH09] Braun and Hack, "Register Spilling and Live-Range Splitting for SSA-Form Programs", CC 2009: https://pp.ipd.kit.edu/uploads/publikationen/braun09cc.pdf
- [BMH10] Braun, Mallon, Hack, "Preference-Guided Register Assignment", CC 2010: https://pp.ipd.kit.edu/uploads/publikationen/braun10cc.pdf
- [BOI09] Boissinot et al., "Revisiting Out-of-SSA Translation", CGO 2009 (report copy): https://bernsteinbear.com/assets/img/boissinot-out-ssa.pdf
- [RSL08] Rideau, Serpette, Leroy, "Tilting at windmills with Coq": https://xavierleroy.org/publi/parallel-move.pdf
- [COL11] Colombet et al., CASES 2011 (abstract on an index page): https://search.isc.ac/inventory/49/2073157.htm
- [FIRM] https://raw.githubusercontent.com/libfirm/libfirm/master/ir/be/bechordal_main.c, https://raw.githubusercontent.com/libfirm/libfirm/master/ir/be/bespillbelady.c, https://raw.githubusercontent.com/libfirm/libfirm/master/ir/be/beprefalloc.c
- [FIRMCH] https://raw.githubusercontent.com/libfirm/libfirm/master/ir/be/bechordal.c
- [CLOLD] https://raw.githubusercontent.com/bytecodealliance/wasmtime/v0.20.0/cranelift/doc/regalloc.md
- [PS99] Poletto and Sarkar, "Linear Scan Register Allocation", TOPLAS 1999: https://www.cse.iitm.ac.in/~krishna/cs3300/linearscan.pdf
- [TRAUB98] Traub, Holloway, Smith, PLDI 1998: https://bernsteinbear.com/assets/img/quality-speed-linear-scan-ra.pdf
- [WM05] Wimmer and Mössenböck, VEE 2005: https://oberon.ssw.jku.at/Research/Papers/Wimmer05/Wimmer05.pdf
- [WF10] Wimmer and Franz, "Linear Scan Register Allocation on SSA Form", CGO 2010: https://bernsteinbear.com/assets/img/wimmer-linear-scan-ssa.pdf
- [GORA] https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/regalloc.go (header comment)
- [GORA24] https://github.com/golang/go/blob/release-branch.go1.24/src/cmd/compile/internal/ssa/regalloc.go
- [GO14850] https://github.com/golang/go/issues/14850
- [GO45897] https://github.com/golang/go/issues/45897
- [GO16122] https://github.com/golang/go/issues/16122
- [V8RA] https://github.com/v8/v8/blob/main/src/compiler/backend/register-allocator.h
- [V8TS] https://github.com/v8/v8/blob/main/src/compiler/turboshaft/register-allocation-phase.h
- [V8GREEDY] https://codereview.chromium.org/2060673002
- [V8MID] https://github.com/v8/v8/commit/cb58ae3b26f8
- [MAGLEV] https://v8.dev/blog/maglev
- [B3] https://webkit.org/blog/5852/introducing-the-b3-jit-compiler/
- [AIRGEN] https://github.com/WebKit/WebKit/blob/b942617ff39a/Source/JavaScriptCore/b3/air/AirGenerate.cpp
- [AIRGREEDY] https://github.com/WebKit/WebKit/commit/f74305d8e550, https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/b3/air/AirAllocateRegistersByGreedy.cpp
- [AIRRM] https://github.com/WebKit/WebKit/commit/a8f748629bbf, https://github.com/WebKit/WebKit/commit/bfb1b1183bc2
- [ION] https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md
- [RA2] https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/
- [FASTALLOC] https://github.com/bytecodealliance/regalloc2/blob/main/doc/FASTALLOC.md
- [WT9611] https://github.com/bytecodealliance/wasmtime/pull/9611
- [RA3] https://github.com/Amanieu/regalloc3/blob/master/DESIGN.md
- [RA3Z] https://bytecodealliance.github.io/zulip-archive/stream/217117-cranelift/topic/Public.20release.20of.20regalloc3.html
- [CLREMAT] https://bytecodealliance.github.io/zulip-archive/stream/217117-cranelift/topic/Rematerialization.3A.20egraph.20vs.20regalloc.html
- [LJ09] http://lua-users.org/lists/lua-l/2009-11/msg00089.html
- [LJASM] https://raw.githubusercontent.com/LuaJIT/LuaJIT/v2.1/src/lj_asm.c
- [QBE] https://c9x.me/compile/
- [QBESPILL] https://c9x.me/git/qbe.git/tree/spill.c
- [QBEREGA] https://c9x.me/git/qbe.git/tree/rega.c
- [ZIGRM] https://raw.githubusercontent.com/ziglang/zig/0.14.0/src/register_manager.zig
- [MIR] https://raw.githubusercontent.com/vnmakarov/mir/master/README.md
- [LSRA] https://github.com/dotnet/runtime/blob/main/doc/design/coreclr/jit/lsra-detail.md
- [LSRAGA] https://kunalspathak.github.io/2021-07-22-Genetic-Algorithms-In-LSRA/
- [AG01] Appel and George, "Optimal Spilling for CISC Machines with Few Registers", PLDI 2001: https://pages.cs.wisc.edu/~fischer/cs701/pldi-cisc.spilling.pdf
- [KOES] Koes, thesis: https://www.cs.cmu.edu/~dkoes/research/dkoes_thesis.pdf
- [KG09] Koes and Goldstein, "Register Allocation Deconstructed", SCOPES 2009: https://www-new.llvm.org/pubs/2009-04-SCOPES-RegisterAllocationDeconstructed.pdf
- [UNISON] Castañeda Lozano et al., TOPLAS 2019: https://arxiv.org/pdf/1804.02452
- [RL4REAL] https://arxiv.org/pdf/2204.02013
- [VERILOCC] https://arxiv.org/html/2506.17506v1
- [CAI25] https://api.crossref.org/works/10.1145%2F3669940.3707286 (existence), https://arxiv.org/abs/2602.07627
- [SRH04] Smith, Ramsey, Holloway, PLDI 2004 (abstract): https://www.cs.tufts.edu/~nr/pubs/gcra-abstract.html
