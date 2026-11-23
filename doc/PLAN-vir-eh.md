# Exceptions in VIR and the back ends (steps E0-E8)

Itanium C++ ABI zero-cost exceptions (level I unwinding, level II
`__cxa_*`, an LSDA in the GCC format) for arm64 and x86-64 Linux, through
`-O1` (the direct emitter) and `-O2`/`-O3` (VIR).  Planned 2026-11-23; no
code yet.

## Scope

| Feature | Back end work | Where it belongs |
|---|---|---|
| Exceptions: unwind edges, landing pads, `.gcc_except_table`, personality in `.eh_frame` | **This plan** | VIR format, builder, both lowerings, the direct emitter, the object writer |
| RTTI (`_ZTI`/`_ZTS`, `typeid`, `dynamic_cast`) | None: data objects and calls | `PLAN-cxx-sema.md` S7 (`abi/itanium/rtti.cpp`); the LSDA's type table only refers to `_ZTI` symbols |
| Modules | None: a front end matter (BMIs) | Not planned |
| Vector extensions | Already in VIR (`SPEC-vir.md` 7.5); C++ lowers to C's AST | Gaps show up in `bench/vir-failures.py`, not here |

Not in scope: Windows x64 (MinGW's SEH unwinding, `.xdata` handler data;
a later WIN step reuses E1-E3), wasm, `-fnon-call-exceptions` (only calls
throw), `-fsjlj-exceptions`, Apple's compact unwind (`.eh_frame` with
personality is enough on Darwin too, as clang emits for functions with an
LSDA).

## Ready for the front end, not depending on it

The C++ front end lowers to C's AST (`frontend/c/ast-builder.h`).  Its
semantic analysis of `try`, `throw` and `catch` comes later
(`PLAN-cxx-sema.md` S7).  This plan defines what it will produce, and tests
the back end without it:

1. **Cleanups are cleanup variables.** The walker already keeps the
   cleanup variables in scope as a tree (`walk.h`: `cleanup_nodes`), and
   x86-64's direct emitter already builds landing pads and
   `.gcc_except_table` from it (`arch/x64/codegen/eh.cpp`, written for the
   first C++ front end, unused since that front end was removed until E1).
   So a cleanup scope is a variable with `cleanup(f)`, from C's attribute
   or the builder's `ast_builder_symbol_cleanup`; there is no new node.
   The run-time symbols the pads call come from one entry point,
   `ast_builder_exceptions_ready(cc, cxx)` (`_Unwind_Resume`; C++:
   `__cxa_begin_catch` and `std::terminate`, C: `abort`; the personality
   routine, `__gxx_personality_v0` or `__gcc_personality_v0`).
   What the C++ front end will need on top (E8, with `PLAN-cxx-sema.md` S7):
   - a cleanup that runs on unwind only: the lowering already destroys
     objects on normal exits itself (`lower/scopes.cpp`), so a destructible
     local gets a flag that keeps the walker from running it again there;
     the declaration follows the construction, so a constructor that throws
     does not destroy the object it did not make;
   - `try/catch`: a body and handlers, each a `_ZTI` symbol (or none for
     `catch (...)`), plus the variable the handler binds; the front end emits
     `__cxa_begin_catch`/`__cxa_end_catch` itself, as calls in the handler
     ([except.handle]).  The back end matches types only through the LSDA.
     A node of C's AST, added with its first back end (E2/E3), not before;
   - a per-call `nounwind` bit and a per-function `noexcept` bit (a
     `noexcept` function gets a terminate landing pad: [except.spec]/5,
     `__clang_call_terminate` as clang);
   - `throw` needs no node: `__cxa_allocate_exception` + `__cxa_throw` are
     ordinary (noreturn) calls the front end builds.
2. **C tests first.** clang accepts `-fexceptions` with
   `__attribute__((cleanup))` in C.  `src/test/vcc/cleanup_unwind.c` raises
   a forced unwind (`_Unwind_ForcedUnwind`, as `pthread_cancel`), so it needs
   no C++ run time; it runs in `vcc-x86.py` (glibc's unwinder; the arm64
   suite's bare-metal run time has none, so there it compiles to an empty
   `main`).  Catching needs C++: `src/test/eh/` with a C++ half built by
   clang++ and the harness `bench/vcc-eh.py` (clang/vcc x thrower/catcher,
   as `vcc-abi.py`) come with E2.  When the C++ front end produces the
   flags and nodes above, `src/test/cxx/run` gains `try`/`catch` cases.

## Design

### VIR format (`SPEC-vir.md`, new section 8.9; opcode table)

- **`invoke`** (terminator, number 87): a call (section 9's signature and
  values) with two successors, the normal block and the unwind block.  Its
  result is a `proj` in the normal block, which must have the invoke's block
  as its only predecessor (as `asmgoto`'s targets, 8.5).  Flags as `call`
  except `NORETURN` (a noreturn invoke has a normal block ending in `trap`).
  A `call` (no unwind edge) inside a function with `VF_EH` may unwind only
  out of the function: it must not throw when the function has a landing
  pad covering it, so the builder emits `invoke` for every potentially
  throwing call inside a scope.
- **Landing-pad block** (`VB_LPAD`): reached only by unwind edges, no
  parameters (8.2 holds: values reach it through slots or dominating defs),
  first instruction `landingpad` of type `multi`: `proj 0` the exception
  pointer (`ptr`), `proj 1` the selector (`i32`).  Its `extra` list is the
  clauses: `cleanup` bit, then catch entries (a symbol, or 0 for catch-all),
  then filter entries (dynamic exception specifications; only the empty
  one, for `noexcept` terminate pads, in 1.0).
- **`resume`** (terminator, 88): continue unwinding with the exception
  pointer (lowered to `_Unwind_Resume`).
- **`ehtypeid`** (`i32` from a `_ZTI` symbol): the selector value for a catch
  clause, compared with the landing pad's selector.  Lowered to the index in
  the function's type table, a constant once the LSDA is laid out.
- **Function flag `VF_EH`** and a personality symbol in the function header
  (`__gxx_personality_v0`; C cleanups under `-fexceptions` use
  `__gcc_personality_v0`, as clang).
- **Verifier rules** (12): an unwind successor is `VB_LPAD`; a `VB_LPAD`
  block has only unwind predecessors and starts with `landingpad`; no
  `landingpad` elsewhere; `resume` only with `VF_EH`; unwind edges are never
  split (a pad needed by two invokes is shared, as LLVM); `ehtypeid` refers
  to a symbol in some clause of the function.  Each gets
  `test/vir/bad/RULE-*.vir`.
- **Text form, parser, printer, interpreter.** `virrun` models unwinding with
  a test-only `throw` external, so passes can be checked on `.vir` units
  without a C++ runtime.

### What passes must know (section 10)

- An `invoke` reads and writes all exposed memory, like a call; on the unwind
  edge every exposed slot may have changed.  Load forwarding and slot
  promotion treat the unwind edge as a call's effects.
- A slot live into a landing pad is live across the invoke (the allocator:
  unwind edges carry no moves, so a value used in the pad must be in a
  callee-saved register or a slot, never a caller-saved one: SPEC 15).
- Simplification may turn an `invoke` of a `nounwind` callee into a `call`
  + `br`, and remove a `VB_LPAD` block that loses its last unwind edge.
- If-conversion, condition merging and the vectorizer skip blocks ending in
  `invoke` (as calls today).  Inlining an invoking callee into an invoke
  rewires its `resume`s into branches to the caller's pad (merged clauses):
  left out at first (E6), the inliner refuses `VF_EH` callees.

### Lowering and objects

- **Call-site table.** Each lowered call in a `VF_EH` function records its
  return address range and its pad (or none); the record optimizer and the
  allocator must not move a call across a call-site boundary.  The LSDA
  (`.gcc_except_table`, uleb128 call sites, action table, type table with
  `DW_EH_PE_pcrel|sdata4` or indirect through `DW.ref.__gxx_personality_v0`,
  as clang) is emitted after the function's code is final; byte-identical to
  clang's for the same call-site layout is the check.
- **`.eh_frame`.** The CIE gains `zPLR` (personality, LSDA pointer
  encoding); the FDE carries the LSDA pointer.  Builds on
  `-fasynchronous-unwind-tables` (`bench/vcc-cfi.py`): with `-fexceptions`
  unwind tables are always on.
- **Landing pad entry.** The unwinder delivers the exception pointer and
  selector in x0/x1 (arm64) or rax/rdx (x86-64); the pad's `landingpad`
  lowers to moves from them.  Callee-saved registers are restored by the
  unwinder; everything else is dead at the pad.
- **The direct emitter** (`-O1`, and VIR's per-function fallback) gets the
  same through the walker's `LO()` interface: `LO(invoke)`, `LO(lpad)`,
  `LO(resume)`.  Its values live in slots across calls already, so only the
  call-site table and the pad entry are new.  VIR may refuse a function with
  a reason (`failure.h`: `eh`) until E5; the fallback must then be correct,
  so the direct emitter comes first.

## Steps

| Step | What | Done when |
|---|---|---|
| E0 | This plan reviewed; clang's output studied on 20 small cases (`-fexceptions` C cleanups, C++ try/catch/cleanup, `noexcept`), notes in `RESEARCH-eh.md` | Plan merged |
| E1 | `-fexceptions` for C as clang (`__EXCEPTIONS`, unwind tables forced on); `ast_builder_exceptions_ready`; C's cleanup variables run on unwind through x86-64's direct emitter; VIR refuses such functions (`exceptions`) so they fall back | `cleanup_unwind.c` at -O1/-O2/-O3 in `vcc-x86.py`; suites unchanged |
| E2 | Direct emitter, arm64: invokes as calls with call-site records, pads, resume; `.gcc_except_table`; personality in `.eh_frame` | `bench/vcc-eh.py` passes at `-O1` under qemu; LSDA byte-identical to clang -O0's on the cases where call sites match; `vcc-cfi.py` unchanged without `-fexceptions` |
| E3 | Direct emitter, x86-64: cleanups exist (E1); add try/catch and the type table | `vcc-eh.py --target x86_64-linux-gnu` at `-O1` |
| E4 | VIR format: `invoke`, `VB_LPAD`/`landingpad`, `resume`, `ehtypeid`, `VF_EH`, personality; text form, verifier rules, interpreter | `bench/vir-test.py` with the new cases and bad units; mutation run clean |
| E5 | VIR builder (the walker's second instantiation) and the passes' rules above; the allocator's rule for values live into a pad | VIR builds every `vcc-eh.py` function (`-fvir-stats`: no `eh` refusals); `-fvir-verify -fvir-check` clean |
| E6 | VIR lowering, arm64 then x86-64; call-site table after the record passes | `vcc-eh.py` at `-O2`/`-O3`, both targets; C suites unchanged at -O1/-O2/-O3 (`vcc-same.py` without `-fexceptions`) |
| E7 | Inlining across invokes; `nounwind` simplification | `src/test/inline` cases with throwing callees; instructions per `vbench.py gate` not worse |
| E8 | The C++ front end's nodes (with `PLAN-cxx-sema.md` S7): `cxx-run` cases with try/catch, destructors on unwind, `noexcept` terminate | `cxx-run.py -O 0123` green; libc++ / nlohmann/json tests that throw (X0 corpora) |

E2/E3 can run in parallel with E4.  CI: `vcc-eh.py` joins `ci/premerge.sh`
at E2 (needs clang++ and qemu, which CI has).

## Risks and open questions

- **Values live into a pad.** Forcing them into callee-saved registers or
  slots may cost in hot functions with many cleanups; LLVM does the same
  (no edge moves on unwind edges), so this is the known price.
- **Call-site table vs. the record optimizer.** Tail merging or moving calls
  across pad boundaries changes which pad covers a call; the optimizer must
  see call-site boundaries as barriers.  Sibling calls are not allowed from
  an invoke.
- **Byte identity with clang.** Call-site ranges depend on our code layout,
  so only the encoding, action and type tables can match exactly; the
  check compares their decoded form otherwise.
- **`setjmp`-like functions** stay refused by VIR, as today.
- Darwin: compact unwind with personality, or `.eh_frame` only?  Decided at
  E2 against what Apple's linker accepts (`vlink-macho.py`).

## Status

- E0 (2026-11-23): plan written.
- E1 (2026-11-23): done.  Found on the way: x86-64's direct emitter had
  cleanup pads, call-site tables and a terminate pad from the first C++
  front end (67e33d83e), dead since its removal (nothing made the run-time
  symbols, nothing emitted `.Lvcc_personality`); E1 makes them from
  `ast_builder_exceptions_ready` and writes the personality word in the
  unit's tail, so the design reuses cleanup variables instead of a new
  cleanup node.  VIR on x86-64 had been building such functions without
  pads; it now refuses them (`VIR: cleanups run by an unwinding exception`).
  `-fexceptions` in C: `__EXCEPTIONS`, and unwind tables on regardless of
  `-fno-asynchronous-unwind-tables`, as clang (C++ on Linux too, since it
  has exceptions by default).  The terminate pad of C calls `abort` without
  `__cxa_begin_catch`, as clang.  Checked: `vcc-x86.py` 2166 (one run
  failure in the first run, gone on rerun), `vcc-ctests.py --suite vcc` at
  -O1 and -O2 368/368, `cxx-run.py` 3196 passed, `vcc-cfi.py` 1637/1637,
  `vcc-driver.py` failures only the host clang's version strings
  (unchanged by E1).  Not done: arm64 (E2), try/catch (E2/E3).
