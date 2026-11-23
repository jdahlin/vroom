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
- Step 0 (`bench/cxx-sema.py --why [N]`, done): 39700 declarations in 1618
  root-cause keys.  Top 10 keys 25 %, top 50 48 %, top 100 61 %, top 200
  75 %; 81 keys of 100 or more hold 22724; 1082 keys under 10 hold 3221 (the
  tail, batched by file).  1020 declarations stay on a "declared by" detail
  the text search could not follow.  The holder is found by searching the
  source text for the name, which is good enough to rank; a front-end field
  would make it exact.
  Top keys: [expr.const] constant-evaluation.cpp:444 1977; [temp.names]
  declared-templates.cpp:820 1781; [temp.inst] instantiation.cpp:557 1663;
  [basic.lookup.qual] name-lookup.cpp:897 1183; [temp.names]
  postfix-expression.cpp:1490 681; [dcl.spec.auto] decl-specifier.cpp:1617
  579; [temp.class] declared-templates.cpp:149 530;
  [basic.scope.declarative] translation-unit.cpp:688 504; [ext.gnu]
  `align_value`-style attributes 458; [dcl.fct]
  parameter-declaration-clause.cpp:474 449.
  The first four (6604) are catch-all sites with no detail: constant
  evaluation that does not know, an instantiation that failed (its reason
  is in `instantiations[].failure` but not reported), a qualified name not
  found, a template-id that names no class or alias template.  Next step
  (1b): make them say why, in their detail text, so that `--why` splits
  them into real causes.  Then work down the list.
- Step 1b (7a3135687 fix, e0ec73bee simplify): the four catch-all refusals
  give a detail (evaluator's last "no rules" line and node kind; what a
  template-name names; an instantiation's inner failure; the held cause of
  a qualified name).  Metrics unchanged; --why keys 1618 -> 1777.  The
  constant evaluator's keys name a vcc source line, so they move when that
  file is edited: compare them only within one batch.  Agent took 24 min.
- Decisions (2026-11-23, from the user): the work list is regenerated every
  5 steps (the next 5 distinct sites, each re-measured by its agent, which
  stops if it fell below about 100); each agent has 15 minutes (soft limit
  10: stop coding, run the gates; hard limit 15: the orchestrator stops it,
  resets, counts a drop).  Gates should be near instantaneous.  Measured
  warm: cxx-sema --check 11 s, cxx-run 25 s, cxx-fe-bench under 1 s.
  cxx-fe-bench flickers by about 0.1-0.2 % with string layout (startup
  strcmp): a harness issue.  Next: step 2, faster gates (cxx-run first).
- Step 2 (03a8f1150 fix, f1f1d92cf simplify): cxx-run caches clang's side
  per program (ref.json) and marks a vcc object byte-identical to one that
  passed against the same reference; keys hold the program, units, flags,
  the tools' identity, the run time, clang's headers and the harness's code;
  vpp always runs; --no-cache / NOCACHE=1.  Warm 25 s -> 8 s.  Per-step gate
  warm now about 20 s (cxx-sema --check 11 s is next to look at).
- Batch 1.1 (f12c397b6, no simplify needed: two lines): a friend function's
  return type may be a placeholder.  Unsupported 39700 -> 39674,
  [dcl.spec.auto] 1103 -> 1000; we accept +2.  Agent 2 min.  What is left
  of decl-specifier.cpp:1617, measured: 488 refusals at -std=c++98 (clang
  takes `auto` there as a C++11 extension with a warning: batch 1.2), 248 at
  -std=c++20 (contexts), and 567 at placeholder-type-specifier.cpp:514.
- Decision: [jdahlin/vroom#550] adds the same cxx-run cache as step 2 and one
  vpp run per test in cxx-sema.  When it lands, merge origin/main at once,
  take its cxx-run.py (carry the texts/files digest split if it lacks it)
  and make --why read the unsupported output from its single run.
- Queue: vpp segfaults on two clang test files (found in the -std=gnu++20
  sweep); `semantic_kind_name` lacks complex_value, complex_part,
  statement_expression (wrong names in tree dumps).
