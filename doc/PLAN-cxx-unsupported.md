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

## Template design note (before the first template step)

The design exists: PLAN-cxx-sema.md 3.5 (an instantiation is a recorded
unit: its key, the template and arguments, and everything it read from
outside) and S6b's status (a class template's specialization is
instantiated by analysing its class-specifier again with the parameters
bound to the arguments and the injected-class-name bound to the
specialization, `instantiate_class` in sema/instantiation.cpp; function
templates the same way per deduced argument list; an instantiation's
errors make the use unsupported, not the template).  Template steps follow
it; none adds a second way to instantiate.

- One mechanism.  New template features extend `instantiate_class`, the
  function template path and the substitution of bound parameters; a step
  that wants a separate copy of the class-specifier analysis stops and
  comes back to the orchestrator.
- [temp.inst]/2: a class template specialization is instantiated only
  where a complete type is needed (today a template-id instantiates); the
  step that changes this goes before the steps that add more instantiable
  forms, since it decides which errors clang would report at all.
- Order, by the work list and by dependency: (1) template-ids in
  expressions and template-names that name what they should ([temp.names],
  postfix-expression.cpp:1490, declared-templates.cpp:854); (2) what class
  templates still lack: default template arguments, non-type parameters,
  members ([temp.class], [temp.param], [temp.mem], [temp.mem.func]);
  (3) explicit specializations and partial specializations
  ([temp.expl.spec], [temp.class.spec], the matching and partial ordering of
  [temp.spec.partial.match]/[temp.func.order]); (4) explicit instantiations
  ([temp.explicit]); (5) member templates, friends of templates, variable
  templates.
- Dependent constructs in a template's own definition are checked only as
  far as [temp.res] requires (no instantiation of the definition); what
  needs an instantiation is decided at instantiation.
- Each specialization is keyed (template, arguments) once: an explicit or
  partial specialization is found by that key before the primary is
  instantiated ([temp.spec.partial.match], [temp.expl.spec]/7).
- Lowering: a specialization's symbol and its vague linkage as the
  existing mangling does; a step changes lowering only where cxx-run or the
  lowering claims need it.

## Agent rules and gates (every brief says: follow this section)

Read: AGENTS.md's "Working conventions" only, this file's Goal, Template
design note and this section, and the log entries the brief names
(doc/PLAN-cxx-unsupported-log.md).  Work in the worktree
.claude/worktrees/cxx-unsupported only.

- No conditional operator (the pre-commit hook rejects it); cite
  `[label]/paragraph` with n4861 labels (bench/cxx-spec-refs.py checks).
- Never `git stash`, never amend or reset a commit you did not make, never
  push.  Revert with `git checkout HEAD -- FILE`.  Rebuild after every
  source change; rerun the gates after any edit before committing.
- Time: `date` at the start; stop coding at 20 minutes, run the gates; you
  are stopped at 25.  Use the time: after a commit go on to the next item.
- A fix that exposes an unrelated bug in newly analysed declarations: if
  that bug is small (about 30 lines), fix it first as its own commit, gated
  alone, then re-apply the fix; if not, save the fix as a patch and report
  the bug's cause and file:line.  One commit per fix, each gated alone.
- clang is the reference; bench/cxx-sema.py runs it as `-cc1 -triple
  aarch64-unknown-linux-gnu` (the test's -triple is dropped).
- Leave no background processes.

Gates (scratch: /tmp/claude-1000/-source-vroom/e06c51d9-10c8-4ffa-8ec6-0c01872e58fd/scratchpad, BASE per step):
  bench/cxx-fe-bench.py --save BASE.json      (first)
  bench/cxx-sema.py -j 8; cp work/cxx-sema-accepted.txt BEFORE.txt   (before editing)
  make -C src -j 8 vcc && make -C src -k -j 8 cplusplus-check
  bench/cxx-sema.py -j 8 --check   (unsupported falls; no refuted or we-reject rise; --bless when only improvements; explain any fall of a *_verified)
  diff BEFORE.txt work/cxx-sema-accepted.txt   (name each new we-accept line's cause)
  bench/cxx-run.py -j 8;  python3 bench/cxx-spec-refs.py;  bench/cxx-fe-bench.py --against BASE.json (+0.5 %)
Blessing (decided 2026-11-24, after the user asked for standing rules).
Agents never run `--bless` and never edit clang-metrics.json,
clang-accepted.txt, clang-failures.txt or known-differences.txt; they leave
an improving change uncommitted and report the `--check` output.  The
orchestrator blesses and commits it, without asking, when all of these hold:
  - no metric row is WORSE, except a *_verified fall whose declarations are
    now rejected as clang rejects them (named in the commit message);
  - `--check` reports 0 new known failures and 0 new we-accept lines;
  - unsupported, we reject and we accept did not rise;
  - every new known-differences.txt entry was re-run against clang by the
    orchestrator, and every new test is accepted or rejected by clang as the
    test says;
  - cxx-run, cplusplus-check and cxx-spec-refs pass, fe-bench within +0.5 %.
Anything else is fixed or dropped, never blessed; a harness change that
moves metrics on purpose goes to the user first.

Commit message: what changed and the numbers, ending with
`Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

Report, at most 15 lines: the commits; unsupported / we reject / we accept
before and after; new we-accept lines with causes; what is left; minutes.

## Status

Current numbers, open queue and decisions: the last entries of
doc/PLAN-cxx-unsupported-log.md.  At 2026-11-23 (tip 610c28655):
unsupported 36649, not parsed 7592, we reject 492, we accept 5423 (main
39530 / 8815 / 677).
