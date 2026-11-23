# Unsupported declarations to zero

`bench/cxx-sema.py` over clang's test/CXX, SemaCXX and SemaTemplate ends a
declaration it has no rule for as `unsupported: [subclause]`.  This plan takes
that count to zero (or to a short, reasoned list), one Sonnet agent at a
time, all on one branch and one PR.

## Baseline (2026-11-23, main at 26d0cd7e8)

4217 runs of 2650 tests.  Declarations: 83682 as clang, 687 we reject and
clang accepts, 4260 we accept and clang rejects, **40598 unsupported**, 8824
not parsed, 728 after a fatal error.

Unsupported by subclause (184 subclauses; a declaration is counted under its
*first* refusal only, so fixing one subclause uncovers the next):
[expr.const] 3333, [temp.names] 3263, [basic.lookup.unqual] 2394,
[ext.gnu] 2373, [basic.lookup.qual] 1435, [ext.clang] 1299,
[temp.expl.spec] 1252, [temp.explicit] 1195, [dcl.spec.auto] 1105,
[class.temporary] 791, [dcl.fct] 737, [temp.class] 699, [temp.inst] 653,
[basic.scope.declarative] 601, [over.match.oper] 592,
[dcl.fct.def.default] 572, [temp.arg.nontype] 564, [namespace.udecl] 540,
[dcl.init] 524, [temp.mem] 510, ... 164 more below 510.

The top 20 hold about 26000 (64 %); the tail of 164 subclauses about 14600.

## What "zero" means

- `unsupported` goes to 0, except entries in
  `src/test/cxx/sema/unsupported-allowed.txt` (new): a test file or
  subclause, one reason per line (out of scope for vcc 1.0: modules, an
  extension we decide not to implement, ...).  Each entry is a user
  decision, not an agent's.
- A declaration leaves `unsupported` for `as clang` or a failure we
  understand.  It may land in `we accept, clang rejects` (a check not
  written yet) — that is counted and is the next plan's list, but it must
  not hide a wrong acceptance of well-formed code as the wrong type:
  declared types, type claims, lookup and layout refuted must not rise.
- `not parsed` (8824) is a separate list (the parser); out of scope here
  unless a step needs it.

## The loop

The orchestrator (an Opus session on branch `cxx-unsupported-zero`, worktree
`.claude/worktrees/cxx-unsupported`) repeats:

1. **Pick.**  Run `bench/cxx-sema.py -j 8 --unsupported 200`; take the
   largest subclause not already tried twice without progress.  Find its
   most frequent *reason* inside the subclause (`-v` on the subclause's
   tests, or `vpp -fcxx-dump=analysis` on a few of them) and cut it to one
   construct an agent can finish in one go: "[temp.names]: a template-id
   naming a member template after `.template`", not "[temp.names]".
2. **Brief** one Sonnet agent (`Agent`, `model: sonnet`, in the same
   worktree, nothing else running) with the template below.
3. **Verify** its commit independently: the diff (spec citations, no
   ternaries, files by topic), `bench/cxx-sema.py --check`, the numbers
   moved as the agent said.  Reject (`git reset --hard` to the previous
   commit) if a gate fails or the change is half an implementation; retry
   once with the reason, else mark the construct "hard" and move on.
4. **Record** the commit and the numbers in the Status section below
   (one line per step), commit that with the step.
5. Every ~10 steps: merge `main` into the branch, rerun the full gates
   (below), push, update the PR's description with the running totals.

One agent at a time: agents share the build tree, the bench caches and the
metrics file; two would race on `clang-metrics.json`.

### Agent brief (template)

> Goal: in vcc's C++ front end (`src/frontend/cplusplus/`), make the
> declarations refused as `unsupported: [SUBCLAUSE]` for CONSTRUCT analysed
> as the standard says.  Examples: FILE:LINE (-std=...), ...
> Spec: `doc/cxx-spec/.../SUBCLAUSE.md`; cite `[label]/paragraph` in
> comments.  Read AGENTS.md (working conventions) and
> doc/PLAN-cxx-sema.md section 5 first.  Rules: no conditional operator;
> nothing half done — what you do not finish stays `unsupported` with the
> reason; clang is the reference (test a small file against it).
> Tests: add a case to `src/test/cxx/` (sema or run) for the construct.
> Done when, in worktree WT:
> `make -C src -j 8 vcc && make -C src -k -j 8 cplusplus-check`,
> `bench/cxx-sema.py -j 8 SUBCLAUSE-TESTS` then `bench/cxx-sema.py --check`
> (bless the metrics with `--bless` only when nothing got worse),
> `bench/cxx-run.py -j 8`, `bench/cxx-fe-bench.py --against BASE` within
> +0.5 %.  One commit, message: what changed and the numbers.  Do not push.
> Report: the commit, unsupported before/after (total and SUBCLAUSE),
> any failure count that moved, what is left in this construct.

### Gates

Per step (the agent, rechecked by the orchestrator): `cplusplus-check`;
`cxx-sema.py --check` (no new failure, no metric worse; unsupported must
fall); `cxx-run.py`; `cxx-fe-bench.py` budget.

Every ~10 steps and before the PR leaves draft: also
`bench/cxx-c-suites.py -j 8`, `bench/cxx-pp.py -j 8`,
`bench/cxx-syntax-conformance.py`, `bench/cxx-spec-refs.py`,
`bench/vcc-ctests.py -j 8` (the C front end shares the lowering), and
`bench/quality.py --check`.

## Order

Largest first, with three exceptions:

- **[ext.gnu], [ext.clang] (3672)**: which extensions to implement is the
  user's call.  The orchestrator lists the extensions by count once and
  asks; the answer goes into `unsupported-allowed.txt` or the queue.
- **Lookup first** ([basic.lookup.unqual], [basic.lookup.qual],
  [basic.scope.declarative]; 4430): everything else is counted behind them,
  so they shrink the hidden work most.
- **Templates as a group** ([temp.names], [temp.expl.spec],
  [temp.explicit], [temp.class], [temp.inst], [temp.mem], ...; ~9000):
  the instantiation model must be one design.  The orchestrator (not an
  agent) writes a short design note in PLAN-cxx-sema.md before the first
  template step; agents then fill it in.

## Size and pace

At 150-400 declarations per step on the large subclauses and 20-100 in the
tail, expect 150-250 steps.  The count will fall slower than the steps
suggest early on (first-refusal attribution uncovers the next refusal).
The orchestrator stops and asks when: a step needs a design decision
(object model, instantiation, an ABI question), three steps in a row are
rejected, or the PR's diff conflicts with main beyond a mechanical merge.

## PR

One draft PR against main, opened after the first step, title "cplusplus:
unsupported declarations to zero".  Each step is its own commit; the
description keeps the running totals table.  Leaves draft when the count
is at the allowed list or the user says to land what is there.

## Status

- 2026-11-23: plan written; baseline above.
