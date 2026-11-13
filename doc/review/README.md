# Quality reviews (PLAN-quality.md, stage P1)

One review per area, each written by a reviewer who read every file of the
area in full.  The reviews are the evidence the quality plan (P3) is built
from; they change no code.

## Areas

| File | Area | Files (under `src/`) |
|---|---|---|
| `quality-lexpp.md` | lexer, preprocessor, header cache | `frontend/lex/`, `frontend/pp/` without `diag*.c` |
| `quality-diag.md` | diagnostics engine; error reporting in every module (L9) | `frontend/pp/diag*.c`, `frontend/c/diag.c`; every `die`/`err`/`error`/`fatal` helper in the tree |
| `quality-cc-expr.md` | C front end: expressions, types, constants | `frontend/c/`: `expr exprop conv const bitcast lit type layout record complex atomic bigint bigfloat sym token` and the three headers |
| `quality-cc-decl.md` | C front end: declarations, statements, flow | `frontend/c/`: `decl declspec declarator attr init stmt func flow lazy tu mangle fmt` and the three headers |
| `quality-cc-cache.md` | C front end: declaration replay, snapshot, builtins | `frontend/c/replay.c`, `frontend/c/builtin.c`, `driver/ccsnap.[ch]` |
| `quality-codegen-shared.md` | shared code generation | `codegen/*.[ch]`, `codegen/walk/`, `codegen/opt/`, `codegen/rec/`, `codegen/flow/` |
| `quality-walker-a64.md` | arm64's AST walker against the shared walker, concept by concept | `arch/a64/codegen/` (not `opt func print obj darwin tail unreachable insn`), read next to `codegen/walk/` |
| `quality-walker-wasm.md` | wasm's AST walker against the shared walker | `arch/wasm/codegen/`, read next to `codegen/walk/` |
| `quality-backend-records.md` | records after the walk: clean-ups, printers, encoders; x86-64's lowering | `arch/a64/codegen/{opt,func,print,obj,darwin,tail,unreachable,insn}.c`, `arch/x64/codegen/` without `vir.c`, `codegen/rec/`, `codegen/flow/` |
| `quality-vir-ir.md` | VIR: format, parser, verifier, analyses, allocator | `vir/` |
| `quality-vir-build.md` | VIR: builder, vectorizer, both lowerings | `codegen/vir/`, `arch/x64/codegen/vir.c` |
| `quality-as-obj.md` | assemblers, object model, object writers | `as/`, `arch/*/as/`, `obj/`, `arch/*/obj/`, `platform/linux/elf.c`, `platform/darwin/macho.[ch]`, `platform/wasm/wasm.c` |
| `quality-linkers.md` | linkers, objcopy/nm, archives | `ld/`, `arch/*/ld/`, `platform/darwin/ldmacho.c`, `vccd/ar.[ch]` |
| `quality-driver-server.md` | driver, server, shim, shell; environment knobs (L8) | `driver/` without `ccsnap`, `vccd/` without `ar`, `src/include/protocol.h`; every `getenv` in the tree |
| `quality-lib.md` | shared library and its private variants (L2) | `lib/`; every private vector, buffer, hash, arena, file mapping and byte helper in the tree |
| `quality-scheduling.md` | threads and scheduling (L3), with measurements | every thread, queue, lock and per-thread state |
| `quality-passes.md` | passes and traversals (L4), with their cost | the pipeline of one TU, front end to object |
| `quality-boundaries.md` | module boundaries and dependencies (L7) | every header another module includes |

## Rules for a reviewer

1. Read `AGENTS.md` and `doc/PLAN-quality.md` (principles, lenses L1-L10,
   the survey table: leads to confirm or refute, not facts).
2. **Read every file of your area in full**, top to bottom, in chunks.  Do
   not review from grep hits or excerpts; grep is for finding the other side
   of a duplicate and for counting.  Write findings into your review file as
   you go (after each file), so nothing is lost when your context is
   summarized.
3. Every finding carries evidence you looked at yourself: `file:line` (both
   sides for a duplicate).  Say "inferred" when you did not verify something.
   Do not report taste.  A finding names what it costs today: a second copy
   to keep in step, an extra walk, a name that misleads, a lock that
   serializes, a file nobody can find things in.
4. The duplicate scan knows your area:
   `bench/quality.py --clones 999 --only DIR/ [DIR/...]` (add
   `--min-tokens 40` for shorter runs).  Triage its runs: a real second copy,
   an inherent table shape, or a pattern that wants a helper.  It cannot see
   the same algorithm written differently; reading must find that.
5. Change nothing: no source edits, no commits, no builds, no writes outside
   your one review file (the measurement reviews say what they may run).
   Other reviewers work in this checkout at the same time.
6. English, plain and short.  Paths as they are today (`src/...`,
   written relative to `src/`).  At most about 400 lines.

## Template

```
# Quality review: AREA (DATE, main at REV)

## 1. Summary
Five to ten lines: what state the area is in, the three things that matter most.

## 2. Inventory
One row per file: lines | what it actually does | a better name if this one
hides the job (L5) | header comment: good, label only, none, stale.
Then: what the module is for and the invariants a newcomer must know (L6);
this text is the draft of the module's opening comment.

## 3. Passes and data flow (L4)
What walks what, in which order, how often; what each walk computes; which
walks recompute the same fact; which file holds several passes; which pass is
spread over several files.

## 4. Findings
A table, most valuable first:
| Id | Lens | Class | Evidence | What it costs | Proposed home or fix | Size | Risk | Proof |
Id: Q-AREA-NN.  Lens: L1-L10.  Class: duplicate, near-duplicate, pattern,
misplaced, misnamed, redundant walk, mixed file, long function, global
state, thread, error policy, dead, stale doc.  Size: S (under a day), M
(days), L (a week or more).  Proof: the harness or check that shows the fix
changed nothing (`vcc-same.py`, `vbench.py gate`, a named suite) or what is
missing to prove it.
Below the table, a paragraph for each finding that needs one.

## 5. Names (L5, L6)
Identifiers that hide their meaning: name | where | what it is | proposal.
Prefix conventions of the area and where they break.  Words used with two
meanings.

## 6. Decisions needed
Choices the plan cannot make without Johan or a measurement, each with the
options and a recommendation.

## 7. Not covered
What you did not read or could not verify.
```
