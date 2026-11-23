# Unsupported declarations to zero

## Goal

vcc's C++ front end analyses every declaration in clang's C++ tests
(test/CXX, SemaCXX, SemaTemplate, as `bench/cxx-sema.py` runs them) that
it now refuses as `unsupported`.  Unsupported goes from 39709 to 0, apart
from entries in `src/test/cxx/sema/unsupported-allowed.txt`, each with a
reason the user approved.  The bar for correctness does not drop on the
way: each step must keep refuted claims (declared types, types, constants,
lookup, layout, symbols, lowering) and "we reject, clang accepts" at their
current numbers or lower.

All the work is on one branch, `cxx-unsupported-zero`, in one PR
(jdahlin/vroom#549), one commit per step.  A Sonnet agent does each step,
one agent at a time, and the orchestrator (an Opus session) checks it.

### Milestones

| | unsupported | what gets it there |
|---|---:|---|
| M0 | 39709 | baseline, origin/main 2bb71984a |
| M1 | < 32000 | the work list (step 0), then the largest roots: qualified lookup with no reason, `<compare>`, constant evaluation |
| M2 | < 22000 | templates, after the design note (instantiation, explicit specialization and instantiation, template-ids) |
| M3 | < 10000 | declarations: `auto`, defaulted functions, `using`, friends, temporaries |
| M4 | < 2000 | the tail: 150+ subclauses, batched by file |
| M5 | 0 + allowed list | the extensions the user decided on, the rest |

A milestone is reached when the count is below its number and the full
gates are green.  After each milestone the branch merges main and the PR
description gets the table.

## Why the subclause count is the wrong work list

Step 1 showed it: it removed about 470 refusals in `<immintrin.h>` but
only 9 counted declarations, because the harness counts a declaration
**once**, under its **first** refusal, and only declarations in the test
file itself (headers are not counted).  Many refusals are also
consequences rather than causes: "X is declared by a declaration that is
not parsed / has no rules yet" (more than 3000 in a `-std=gnu++20` sweep)
points at another declaration, and the cause is that declaration's reason.
Picking by subclause, or by counting constructs, aims at the wrong
targets.

## Step 0: the work list (the orchestrator, before any further agent)

`bench/cxx-sema.py --why [N]` writes `work/cxx-sema-why.json` and prints the
top N **root causes**, each with the number of counted declarations it
blocks:

1. For each counted unsupported declaration (`-fcxx-dump=analysis`, in the
   test file), take the first `-fcxx-dump=unsupported` line inside its
   range.
2. Key it by (subclause, vcc source site `file:line`, detail with names
   and numbers made generic).  Many details are empty; the site is what
   tells causes apart.
3. When the detail is "X is declared by a declaration that is not parsed /
   with no rules yet", follow it to X's declaration and use *its* key.
   Repeat until the key is a real cause or the declaration is not parsed;
   "not parsed" becomes a parser key (its node, `-fcxx-debug-fallbacks`).
4. Each key gets a count, three example `test:line -std=` cases, and the
   subclauses it hides behind it, where they can be seen.

The output is the queue.  It is regenerated after every step, and the
step's commit message quotes its key's count before and after.

## A step

- **Pick** the top key of the work list that has not been tried twice.
  Keys at the same site (one function, one missing rule) go to one agent.
- **Size.**  A step should remove at least 100 counted declarations, or
  remove a root that the work list shows hides at least that many.  A
  smaller fix is batched with others in the same subclause.  A step that
  needs a design (object model, instantiation, an ABI question) stops and
  goes to the orchestrator.
- **Brief** (template below): the key, its examples, the spec file, the
  gates, one commit, report numbers.
- **Check** (the orchestrator, every step): read the diff; rerun
  `cplusplus-check` and `cxx-sema.py --check`; inspect any known-failures
  line the step deletes, as in step 1 (removing a failure by skipping a
  check needs a reason the plan accepts); compare the work list before and
  after.  Throw the commit away and try once more with the reason, or mark
  the key hard.
- **Record** one line in Status: the commit, the key, the counts.

Not parsed (8827) is in scope only where the work list shows a parse
failure as a root of unsupported declarations; the rest is the parser's
plan.

### Agent brief (template)

> Goal: in vcc's C++ front end (`src/frontend/cplusplus/`), remove the
> root cause KEY (subclause, site FILE:LINE, detail) that blocks N
> declarations in clang's tests.  Examples: TEST:LINE (-std=...), ...
> Spec: `doc/cxx-spec/.../SUBCLAUSE.md`; cite `[label]/paragraph` in
> comments.  Read AGENTS.md (working conventions) and
> doc/PLAN-cxx-unsupported.md first.  Rules: no conditional operator;
> nothing half done (what you do not finish stays `unsupported` with its
> reason); clang is the reference (test a small file against it); no bare
> `git stash`.
> Tests: a case in `src/test/cxx/` (sema `gram/`/`spec/`, or run).
> Done when, in the worktree:
> `make -C src -j 8 vcc && make -C src -k -j 8 cplusplus-check`,
> `bench/cxx-sema.py -j 8 --check` (bless only when nothing got worse;
> commit the metrics), `bench/cxx-sema.py --why 30` before and after,
> `bench/cxx-run.py -j 8`, `bench/cxx-fe-bench.py --against BASE` within
> +0.5 % (save BASE before changing anything).
> One commit: what changed and the numbers.  Do not push.
> Report: commit, KEY count before/after, total unsupported before/after,
> any other metric that moved and why, the new top keys this exposed.

### Gates

Per step: as in the brief.  At each milestone, and before the PR leaves
draft: also `bench/cxx-c-suites.py -j 8`, `bench/cxx-pp.py -j 8`,
`bench/cxx-syntax-conformance.py`, `bench/cxx-spec-refs.py`,
`bench/vcc-ctests.py -j 8`, `bench/quality.py --check`, and the asan build
over `cxx-sema.py` (`--bin-dir build/linux/src-asan`).

### Decisions for the user

- **Extensions** ([ext.gnu] 2373, [ext.clang] 1299 by subclause): at M1 the
  orchestrator lists them by work-list count; each is implemented or
  allowed.
- **The template design note** before M2, written by the orchestrator and
  reviewed by the user.
- **Harness artifacts** such as step 1's `-mavx` case (clang is run
  without the test's target flags): fix the harness or list the case in
  known-differences.

### Stop and ask

When three steps in a row are thrown away, a key needs a design, a metric
other than unsupported gets worse and the cause is not a known artifact,
or merging main into the branch is more than mechanical.

## Size

There are about 200 keys in total (184 subclauses, several sites each).
At an average of 200 declarations per step, that is about 200 steps;
after step 0 the work list gives a better estimate, recorded here.

## Status

- 2026-11-23: plan written; baseline above (on a stale main; origin/main
  2bb71984a: 39709 unsupported, 2002 known failures).
- Step 1 (7523417ef, x86 vector typedefs): a function-definition with an
  unparsed body held its return type's typedef (`__m128` in <f16cintrin.h>),
  refusing every later use; now it holds only its declarator-id.  Tree check
  skips deferred system-header bodies (1129 `no expression` entries gone).
  Unsupported 39709 -> 39700, known failures 2002 -> 873, types verified +441.
  Next in those headers: `_Float16 _Complex`, `align_value`, `__fp16` bit
  casts, `__builtin_ia32_*`/`__builtin_elementwise_*` builtins.
- Queue: vpp segfaults on two clang test files (found in the -std=gnu++20
  sweep); `semantic_kind_name` lacks complex_value, complex_part,
  statement_expression (wrong names in tree dumps).
