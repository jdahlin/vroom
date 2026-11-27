# PLAN-cxx-snapshot: analysed C++ headers once per build, not once per TU

Status: CS2-CS4 done (2026-11-27); CS4's results and the decision it asks for
are there.  CS5 on not started.  Steps CS0-CS11, section 7; update a step's
status there.

Goal given: Stockfish 17.1 (`work/stockfish/src`, 23 TUs, libc++) compiled
about 30x faster than clang++.  Which metric (cold build, rebuild, CPU, wall)
is decision D1.

How to read this document: a claim marked **[V file:line]** was read in the
source at commit 122c00874, **[M n]** was measured here (section 1 has the
command), **[A]** is an assumption that a named step checks.  Code sketches
never use the conditional operator.

## 0. Summary

1. **What to share.**  A *prefix*: a list of headers analysed before the main
   file, with exactly the meaning of clang's `-include PREFIX`.  For Stockfish
   the 52 system headers its TUs include.  Every TU compiles on it today, with
   vpp and with clang++, and defines the same symbols with the same sizes as
   with its own includes [M 3, M 7].
2. **How to share.**  Recommended: a *prefix worker*, a single-threaded process
   that analyses the prefix and then forks one child per TU; the child goes on
   with the TU's own file.  The kernel's copy-on-write is the snapshot.  The
   front end's state is about 200 containers in 8 objects that refer to each
   other (more with the per-file and per-scope objects), mutated in place
   everywhere; a fork needs no inventory of them, an
   in-process copy needs a complete and permanently maintained one.  Measured
   restore cost at 16 concurrent jobs: fork 8-27 ms CPU, copy-on-write mapping
   in threads 22-55 ms, memcpy 54-65 ms [M 11].  The in-process alternative is
   planned as plan B (section 4.7) in case processes are ruled out (D2).
3. **What it gives** (instructions, all 23 TUs, `-O2 -c`):
   today 82.4G; system prefix 32.4G (2.5x); system and project headers 19-23G
   (3.6-4.2x).  Against clang++'s 24.8 s CPU that is 3.1x today, about 7x with
   the system prefix, 10-12x with project headers as well.
4. **30x is not reachable by sharing headers.**  30x of clang's CPU is 0.83 s,
   about 8.6G instructions.  With *every* header analysed once, what is left is
   the TUs' own text (3.9G), lowering and code generation (11.5G) and the
   prefix itself (4.0G): 19.4G.  The backend alone is above the budget.
   Reaching 30x on a cold build needs, on top of this plan, a backend about 3x
   faster or reuse of generated code.  30x *is* within reach for other
   metrics: `-fsyntax-only` CPU (21x predicted), a rebuild after an edit in a
   warm server, and anything the result cache (`bench/stockfish/cache.py`)
   answers.
5. **Largest risks.**  (a) The forced prefix changes what a TU sees; for
   project headers it changes objects today [M 8].  (b) The front end parses
   the whole TU before it analyses anything; the prefix needs a stop between
   the two halves, in all four phases.  (c) The main file is given its source
   locations before the prefix's files [V preprocessing-file.cpp:148].
   (d) Process plumbing in vccd.

## 1. Measurements

x86-64 Linux, 16 hardware threads, load average 12-20 during the session, so
instruction counts (`perf stat -e instructions:u`) are the numbers to trust;
seconds are `task-clock` of the same runs.  vpp is `build/linux/src/vpp`,
which `make -C src -j 6 vcc` reported up to date at 122c00874.  Flags, as
`bench/stockfish/threads.py`:

```
F="-x c++ -std=c++17 -fPIE -nostdinc++ -isystem /usr/lib/llvm-23/include/c++/v1 \
   -DNDEBUG -DIS_64BIT -DUSE_PTHREADS -DNNUE_EMBEDDING_OFF"
cd work/stockfish/src
```

`prefix.h` is one `#include <H>` per system header named in an `#include <>`
of any Stockfish `.cpp` or `.h`, less the ones the configuration does not reach
(`windows.h`, `intrin.h`, `direct.h`, `arm_neon.h`, `*mmintrin.h`): 52 lines,
sorted by name.  `prefix2.h` is `prefix.h` plus all 35 project headers by
absolute path (87 lines).

| n | what | command | result |
|---|---|---|---|
| 1 | own includes, front end | `vpp $F -fsyntax-only T` per TU | 70.96G in total |
| 2 | prefix alone | `vpp $F -fsyntax-only prefix.cpp` | 2.94G, 0.45 s user, peak RSS 153 MB; clang++ 3.42G |
| 3 | each TU on the prefix | `vpp $F -fsyntax-only -include prefix.h T` | 84.86G, 23 of 23 accepted; less 23 x 2.94G: **17.24G** for the 23 "rests" (0.05G memory.cpp ... 1.61G syzygy/tbprobe.cpp) |
| 4 | system and project headers | the same with `prefix2.h` | alone 4.00G (RSS 220 MB); TUs 95.86G, 23 of 23 accepted; rests **3.93G** |
| 5 | with code | `vpp $F -O2 -c T` | 82.42G, 7.87 s; lowering and code = 82.42 - 70.96 = **11.46G**; on prefix.h that difference is 12.25G, on prefix2.h 15.19G |
| 6 | clang++ 23 | the same commands | `-fsyntax-only` 83.65G; `-O2 -c` 127.35G, 24.77 s; with a PCH of prefix.h: 4.91G to build, 36.89G for the 23 TUs `-fsyntax-only` |
| 7 | objects, own includes against `-include prefix.h` (`-O2 -c`) | `cmp`; `nm -S --defined-only` without `.L*` and `GCC_except_table*` | byte-identical: vpp 5 of 23, clang 18 of 23.  Defined symbols with sizes and types: identical in all 23 for both (vpp 2334, clang 1507).  What differs: vpp's `.L.str.N` numbers and function order; clang's `GCC_except_tableN` numbers; and vpp adds an undefined `__gxx_personality_v0` with an 8 byte `.data.rel.ro` to memory.cpp.o and movegen.cpp.o |
| 8 | the same against `-include prefix2.h` (vpp) | | 14 of 23 TUs differ in defined symbols: `Stockfish::SYSTEM_THREADS_NB` (an inline variable of numa.h with a dynamic initializer) with its guard and a larger `_GLOBAL__sub_I_*` in TUs that never include numa.h; `get_process_affinity`; `deque<StateInfo>::__block_size`; `UCIEngine::move` 0x2f1 bytes instead of 0x2d2 |
| 9 | where the instructions go | `perf record -e instructions:u`, symbols by class | prefix alone: preprocessor and lexer 37 %, parser 23 %, analysis 19 %, libc 13 %, phase 7 3 %.  uci.cpp `-fsyntax-only`: analysis 38 %, preprocessor and lexer 26 %, parser 15 %.  uci.cpp `-O2 -c`: lowering and mangling 6 %, C side (VIR, code, object) 10 % |
| 10 | sizes | `-fcxx-dump=parser-stats`, `=stats`; `sizeof` from a test program | prefix: 617 841 tokens, 350 076 nodes, 5 401 top-level declarations, 82 762 entities, 22 740 types.  `entity` 456 bytes (37.7 MB), a token 72 bytes over four arrays (44.5 MB), `syntax_node` 20, `diagnostic_message` 1040, `semantic_analysis` 4424 |
| 11 | cost of a private copy of 130 MB that a job reads 60 % and writes 10 % / 30 % of (pages) | a C program: `malloc`+`memcpy`; `memcpy` into a kept buffer; `mmap(MAP_PRIVATE)` of a memfd; `fork` | CPU per job, 1 job at a time: 27 / 9.4 / 5.3-10.4 / 5.4-10.8 ms.  16 at a time: 65 / 54-61 / 22-55 / **8.5-27** ms (wall per job slot 168-178 / 82-84 / 53-108 / 18-41 ms) |
| 12 | wall, 16 jobs, loaded machine | `xargs -P 16`; `threads.py -j 16 --src ...` | clang++ 4.45 s; vpp 1.93 s; vpp on prefix.h, no sharing 2.16 s.  `vpp --batch -j 16`: 3.02 s, job sum 36.5 s against 17.9 s serially, peak RSS 2.83 GB |
| 13 | system calls of one TU | `strace -f -c vpp $F -O2 -c uci.cpp` | 11 646 `newfstatat` (1 089 fail), 1 091 `openat`; no thread is created |
| 14 | the forced prefix elsewhere, clang++ only | nlohmann/json's 71 test TUs, prefix of the 38 system headers they include | 71 of 71 accepted both ways; 12 sampled TUs at `-O0 -c`: the same defined symbols and sizes |
| 15 | code emitted more than once | `nm -S` over vpp's 23 objects | 834 KB of code in 2191 symbols; 679 KB in 1413 distinct ones: reuse of generated weak functions saves at most 19 % of the code |
| 16 | exact prefix sharing | `clang++ -H`: the sequence of files each TU enters | each of the 23 TUs starts with a different header; the longest sequence one TU shares with another is 0 of 22 618 file entries |

The task statement called 71.0G a figure from before 122c00874; it was
measured with that change already built, which is why M 1 matches it.  The
commit's 11 % is of `-O2 -c` over the 23 TUs (92.7G to 82.5G, the 82.42G of
M 5).  All numbers here are of 122c00874.

Not measured, and what stands in for it: the pages a TU writes of the prefix's
state (M 11 assumes 10-30 %; CS4 measures it), fork cost of the real process
(M 11 forks a 130 MB process; CS4), macOS (D2).

## 2. What exists after a prefix (question 1)

### 2.1 The objects and their order

One TU is eight objects on the stack of `compile()`, made in this order, each
holding C++ references to earlier ones [V lower/compile.cpp:376-418, 213-224]:

```
diagnostic_options settings; implementation_defined_behavior behavior;   (values)
diagnostic_consumer diagnostics(settings);
source_files        files(behavior, diagnostics);
preprocessor_options options;                       (pointers into the driver's job)
preprocessor        pp(files, options);
token_stream        tokens(pp);
syntax_tree tree; declared_names names;
parser              parse(tokens, tree, names, diagnostics);
semantic_analysis   analysis(tree, tokens, parse, diagnostics);
cc *c = ast_builder_open(p, co); c_lowering lowering(analysis, tokens, c, ...);
```

The phases run one after the other over the *whole* TU: `parse.translation_unit()`
builds the complete tree, then `analysis.translation_unit()` analyses every
top-level declaration and ends with `instantiate_pending()`, then lowering
[V compile.cpp:219-254; sema/semantic-analysis.cpp:544-586].  The token stream
is the only lazy part: it pulls from the preprocessor as the parser asks
[V parse.h:100-106].  **Nothing today stops between "the prefix's
declarations" and "the TU's"; that is the first thing to build (CS2).**

### 2.2 How state is stored

- `owned_array<T>`: a `malloc`/`realloc` block, `T` trivially copyable and
  trivially destructible by `static_assert` [V owned.h:30-35, 73-74].  Elements
  refer to each other by 32 bit index (`token_index`, `node_index`,
  `entity_index`, `type_index`, identifier numbers), not by pointer.  This is
  what makes any snapshot possible: an array's bytes mean the same at another
  address.
- `owned_objects<T>`: one `malloc` per object, addresses stable, used where
  objects refer to each other by pointer or reference [V owned.h:227-266]:
  `source_files::entry` (file, lexer, name) [V cplusplus.h:626-642, 727],
  `declared_names::scope` (three arrays each) [V parse.h:545-562],
  `semantic_analysis::detail_texts` [V sema.h:2229].
- `owned_buffer`: a `vbuffer` whose first 128 bytes are inside the owner, so
  the owner contains a pointer into itself and "does not move" [V owned.h:162-208].
- `record_chains`: two arrays, brought up to date lazily by `update()`, in
  several places from `const` functions through `mutable` [V owned.h:317-354;
  sema.h:1097, 2960, 5669, 5728, 6549; parse.h:546].  A "const" analysis is
  therefore written by readers: a frozen base cannot be read by two threads
  without first forcing these.
- Hash tables are open-addressed slot arrays next to their records
  (`preprocessor::identifier_slots`, `types::slots`, `entities::slots`,
  `expression_node_slots`, `declared_names::by_name`): an insert writes a
  random slot, a growth rewrites all.
- Raw pointers inside stored records, complete list [V grep of pointer members
  in sema.h, parse.h, cplusplus.h]: `entity::held_because`,
  `entity::unknown_because`, `unsupported_construct::{subclause,file,function}`
  and the parser's `unparsed_reason::{expected,subclause}` point at string
  literals of the binary; `unsupported_construct::detail` points into
  `detail_texts`; `preprocessor::inclusion::file` and `main_file` are
  `source_files::entry *`; `diagnostic_options::group_settings[].group`,
  `preprocessor_options::include_directories[].path` and `source_files::directory`
  point into the driver's job.  Transient pointers that must be null between
  top-level declarations: `analysing`, `declaring_lambda`, `collected_names`,
  `defining_bodies`, `clause_packs` [V sema.h:6504 and the grep; that they
  are null there: A, CS2 asserts it at the boundary].
- Thread-locals: `syntax_ambient`, a pointer to the analysis' `SyntaxContext`
  [V syntax.h:67; sema.h:7730-7731], and five variables of the last
  constant-evaluation refusal [V expression/constant-evaluation.cpp:48-59].
  The task statement says there are no other writable globals; not re-audited
  here [A].

### 2.3 Inventory

"Writes" says how a TU continuing from the prefix changes the prefix's part.

| state | where | stored as | writes after the prefix | what is hard |
|---|---|---|---|---|
| macros | `preprocessor::macros`, `elements`, `parameters`, `definition_order`, `pushed_macros`, `unused_candidates` [V cplusplus.h:1009-1019, 1120] | arrays, indices | `used` and `being_replaced` flags at every expansion; `identifier_entry::macro` at each `#define`/`#undef` | flags scattered over the whole array |
| identifiers | `identifiers`, `identifier_spellings`, `identifier_slots` [V :892-899] | records, characters, hash slots | every new spelling: slot insert, growth | the numbers are used by parser and analysis (`tokens.pp.identifier()`, 64 call sites in the front end): the table is part of every later phase |
| include guards, once, entered files | `guarded_files`, `once_files`, `entered_files`: (device, inode[, guard name]) [V :976-989; include-directive.cpp:240-262, 312-333] | arrays, searched linearly | appended | keyed by inode: a prefix file replaced on disk gets a new inode and would be read again; validation must catch it first (6.5) |
| inclusion stack, conditionals | `inclusions` (holds `entry *`), `conditionals` [V :908-924, 948-953] | arrays | at the boundary exactly one inclusion must be left and no conditional open | `start()` pushes the main file *first* [V preprocessing-file.cpp:148-158] |
| pragma state | `source_files::layout_pragma`, `pack_pragma`, `pack_stack`, `ms_struct_pragma`, `target_strings`, `target_stack`, `floating_point_pragma` [V :659-680]; diagnostic mappings in `diagnostic_consumer::mappings`, `mapping_pushes` [V :289-294] | scalars, small arrays | continue | must be at their defaults at the boundary, or the prefix is refused (a header that leaves `#pragma pack` open) |
| `__COUNTER__`, date, time | `counter`, `date`, `time_of_day` [V :1160-1161]; date and time are set in `start()` [V preprocessing-file.cpp:160-170] | scalars | counter continues | date and time are the prefix's unless set again at the boundary; a prefix that *expanded* `__DATE__`, `__TIME__`, `__TIMESTAMP__` or `__BASE_FILE__` cannot be shared (6.5) |
| dependencies for `-MD` | not kept by the C++ preprocessor: `note_dependency` calls the driver's callback, which appends to the C `pp` of the job [V include-directive.cpp:38-44, 244; compile.cpp:198-201] | the C preprocessor's list | appended; the TU's own `#include <vector>` is noted again even though the guard skips the file [V include-directive.cpp:244 before :257] | the prefix's files are all in the list (6.6) |
| diagnostics | `diagnostic_consumer::messages` (1040 bytes each), counts, `suppressed` [V :246-295] | array | appended; rendered at the end of the TU [V compile.cpp:420-421] | warnings of the prefix repeat in every TU, as with `-include` |
| source files and locations | `source_files::files`: per file the text with 16 zero bytes, line starts, line directives, a lexer with its literal values and string code units; `next_base`; `system_markers` [V :149-176, 586-600, 727-735] | objects with arrays; locations are offsets in one 32 bit space, a file's base given when it is opened [V lexer/source-files.cpp:173-180] | new files appended; scratch files (pasted and stringized tokens) appended | each file is read with `fopen`/`fread` per TU, no shared cache [V source-files.cpp:31-46]; the main file's base precedes the prefix's files (2.1) |
| tokens | `token_stream::tokens`, `preprocessing_tokens`, `first_piece`, `identifiers`, layout and target change lists, one preprocessing token of lookahead [V parse.h:78-98] | arrays | appended; `name_token()` writes `identifiers[i]` of an existing token | the lookahead (after a string literal, after a revertible keyword) reads one preprocessing token past the token asked for [V parse/token-stream.cpp:75-115]: at the end of the prefix that would be the main file's first token |
| syntax tree | `syntax_tree::nodes`, `child_lists`, `split_tokens`, `template_names_as_types`, `kind_counts`, `root` [V parse.h:269-290] | arrays | appended (a failed tentative parse truncates, never below the current declaration) | `root` and the outer `declaration_seq` exist only when the TU ends [V basic/translation-unit.cpp:17-32]; `SyntaxContext` copies the two data pointers and says the tree "must not grow while one is in use" [V syntax.h:50-63] |
| the parser's names | `declared_names::scopes` (per scope: declarations, hash, searched-with), `journal`, `lookups`, `member_template_names` [V parse.h:504-574] | objects with arrays, one `malloc` each | global and namespace scopes of the prefix get the TU's declarations | pointerful; one scope per class, block, prototype and template head |
| parser | `position`, `pending`, `unparsed_reasons`, `specialization_regions`, keyword tables, about 40 flags [V parse.h:784-1491] | scalars, arrays | continues | between top-level declarations the flags are at rest [A; CS2 asserts] |
| types | `types::records`, `parameter_lists`, `slots` [V sema.h:247-250] | hash-consed records | appended, slots written | |
| entities | `entities::records` (456 bytes each), `declarations`, `slots`, `c_linkage` [V sema.h:1081-1097] | records, chains, hash | **in place, constantly**: `defined`, `member_definition_needed`, completion of classes, redeclarations, chains | the largest array (37.7 MB for the prefix) and the most written |
| analysis | `semantic_analysis`: at least 141 containers (a grep of its member declarations; 27 more in parse.h, 36 in cplusplus.h) and several hundred scalars in one class of 4424 bytes [V sema.h:1880-7731; M 10]: declarations, expressions, semantic tree, definitions, class members and bases, vtables, friends, using-directives, lambdas, constant-evaluation state | arrays | appended and written | size; nested structs with their own arrays |
| instantiations | `function_templates`, `instantiations`, `instantiation_inputs`, `instantiation_values`, `template_arguments`, `pack_arguments`, `canonical_packs`, `satisfactions`, `specializations_instantiated` and their `record_chains` [V sema.h:5666-6549] | arrays, chains | a TU's uses add specializations of the prefix's templates and mark existing ones needed | this is where a TU's cost after the prefix is: uci.cpp's rest is 1.57G [M 3] |
| deferred, end of TU | `pending_definitions`, `postponed_bodies`, `nested_bodies`, `deferred_needs`, `deferred_class_instantiations`, `pending_uses`, `pending_defaults`, `pending_noexcepts`, `pending_diagnose_ifs`, `explicit_instantiations`; drained by `instantiate_pending()` once, after the last declaration [V sema.h:5829, 6613; sema/instantiation.cpp:1352-1422; semantic-analysis.cpp:585] | arrays | the prefix's pending work is carried into every TU and done there, or never | nothing may be drained at the boundary: what a prefix's inline function needs is instantiated only if the TU uses it (3.3) |
| whole-TU reads by the analysis | `allocation_names_spelled()` counts `operator new`/`delete` tokens over `tokens.made()`, all tokens of the TU [V basic/allocation-functions.cpp:206-222; used at expression/new-expression.cpp:245, 636] | | | the one place found where analysing a declaration reads tokens after it; with a stop at the boundary its answer changes (CS2) |
| lowering | `c_lowering` and the C AST `cc`, made per TU after the analysis [V compile.cpp:244-254] | not part of the prefix | | it visits every declared name of the TU twice and every entity once [V lower/definitions.cpp:334, 394, 430], so its cost grows with the prefix: +0.8G for 23 TUs on prefix.h, +3.7G on prefix2.h [M 5]; a TU-wide flag (`eh_personality`, set once per TU [V frontend/c/ast-builder.cpp:383]) is set by something of the prefix and reaches the object [M 7] |
| code generation | the C side: VIR, emitter, object writer, per TU | not part of the prefix | | emits in the order of declaration, so the prefix's order shows in the object [M 7] |
| the driver's job | `driver_job`: main file, output, depfile, predefines, search path; the C `pp` [V driver/job.cpp:223-253, 547-640] | | | the prefix's analysis must not depend on the main file's name; outputs are written after the compile [V job.cpp:482-539], so a child can be given other paths |

## 3. Semantics (question 3)

### 3.1 The definition

Prefix mode is *defined* as: the TU is compiled as if `-include PREFIX` had
been given.  That is a meaning clang has, so clang stays the reference:
`vpp -fcxx-prefix=P x.cpp` must equal `vpp -include P x.cpp` byte for byte
(CS3) and agree with `clang++ -include P x.cpp` where the harnesses compare
with clang.  How a snapshot is restored never changes meaning; only this
definition does.

### 3.2 What differs from "the TU with its own includes"

| # | difference | Stockfish, system prefix | in general | detectable |
|---|---|---|---|---|
| 1 | Declarations and macros of headers the TU never included are visible: an ill-formed TU (a missing `#include`) is accepted; a name can resolve differently (an overload of another header is a better match; `abs` with only `<cstdlib>`); a global of the TU can clash with a C library name | 23 of 23 accepted, same defined symbols and sizes, both compilers [M 3, M 7]; json 71 of 71 [M 14] | [res.on.headers]/1 lets a C++ header include any other, so a portable program cannot rely on a standard name being absent; this implementation's behaviour can still change | yes: record for each name, overload, specialization and macro the TU's own text uses which file declares it, and compare with the files the TU's own includes reach (CS9) |
| 2 | Order: the prefix's headers come before all of the TU's text, in the prefix's order.  A macro the TU defines before an include (`_GNU_SOURCE`, `NOMINMAX`) no longer reaches the header | no difference seen | common in C-style code | yes: a macro the TU defines or undefines whose name the prefix's text spells, before a TU include of a prefix file (CS9) |
| 3 | An include inside a scope (`extern "C" {`, `namespace`) or with a pragma open (`pack`, `visibility`) | none | rare | yes: the include is not at the top level, or pragma state is not the default (CS9) |
| 4 | Order of entities: labels (`.L.str.N`), order of functions in the object, `__COUNTER__` values | vpp 18 of 23 objects differ in bytes, clang 5 [M 7] | always | not a change of meaning; it rules out "byte-identical to the build without a prefix" as the test (D5) |
| 5 | Things a header makes every TU emit: a namespace-scope variable with a dynamic initializer, `__attribute__((constructor))`, an explicit instantiation definition, file-scope `asm` | none in libc++ 23's headers [M 7]; project headers: `SYSTEM_THREADS_NB` appears in 14 TUs that do not include numa.h [M 8] | changes static initialization of TUs | yes: list them when the prefix is built; filter by the TU's reach, or refuse the header as a prefix member (CS9, CS10) |
| 6 | TU-wide facts set by the prefix: vpp's personality reference in two objects [M 7] | 8 bytes and an undefined symbol | | fix in lowering: set it when a function that needs it is emitted (CS1) |
| 7 | Inlining and overload decisions inside the TU's own functions: `UCIEngine::move` changed size on prefix2.h [M 8] | not on the system prefix | project headers | not investigated; CS10 must explain it before project headers are shared |
| 8 | `-MD`: every file of the prefix is a dependency of every TU | 23 depfiles list all files | a superset is safe for a build system, and equals clang's with `-include` | 6.6 |
| 9 | `-H`, `-dM`, `-dD`, the `-fcxx-dump=` outputs | | | prefix mode is off for them |

### 3.3 End-of-TU work: what is reused and what is not

- Reused: preprocessing, lexing, parsing and the analysis of every declaration
  of the prefix; the instantiations the prefix's own declarations required
  *while they were analysed* (a class completed for a member, a constant
  expression evaluated).
- Not reused, per TU: every specialization the TU's own code asks for
  (`std::vector<Move>`, the `basic_string` members it calls), all bodies of
  member functions of class templates (instantiated when used,
  `postponed_bodies`), everything `instantiate_pending()` does.  This is why a
  TU's rest is not small: 17.2G for 23 TUs against 2.94G for all headers
  [M 2, M 3], of which 13.3G go away when the project headers are in the
  prefix as well [M 4] and 3.9G are the `.cpp` files themselves.
- Not reusable in principle with this design: instantiations two TUs both need
  but the prefix does not (they could be put *into* the prefix by a header that
  uses them, which changes #5 above), and all lowering and code.
- Inline functions and static initializers of headers: emitted per TU when
  used, as today; libc++ 23 has no per-TU `ios_base::Init` (no symbol
  difference in M 7).  A prefix cannot make the object smaller or larger
  except through #5 and #6.
- Nothing is drained at the boundary.  If a later change makes the analysis
  flush pending work at the end of the prefix, every TU pays for and may emit
  instantiations it never asked for.

### 3.4 Alternatives that keep the TU's own meaning

- **Prefix tree keyed by the sequence of includes** (what the C declaration
  cache does: a trie of top-level regions, a snapshot per node
  [V driver/declaration-cache.cpp:1-24]).  Exact.  Useless here: every
  Stockfish TU begins with a different project header, no two TUs share any
  leading sequence [M 16].
- **Hoisting only the TU's own system headers** in front, in a canonical
  order, sharing by a trie over that order.  It keeps #1 away but not #2, and
  the 23 sets differ (11 to 28 of 52 headers), so the trie shares little.
  Not pursued.
- **Visibility masking**: the common prefix, and every lookup filtered by the
  headers the TU reached.  Filtering inside the analysis touches every lookup
  path, ADL, specialization selection and completeness; a missed path is a
  silent wrong answer.  The plan uses the same information the other way
  round: *detect* a use of something the TU did not reach and compile that TU
  again without the prefix (CS9).  Detection that misses a case degrades to
  `-include` meaning, never to a meaning no compiler has.
- **Module-like units per header** (clang header units, `import std`): each
  header analysed alone and merged.  Needs merging of redeclarations across
  units and a remapping of every index; `doc/RESEARCH-compile-server.md` names
  that merging as clang's most bug-prone machinery.  Headers are also not
  modular (macros in, macros out).  The right end state for a server that
  shares more than a prefix; far beyond this plan.

## 4. Mechanisms (question 2)

All of them need CS2 (a stop at the boundary) and section 6.5 (key and
validation); they differ in how a TU gets a private, writable view of the
prefix's state: about 130 MB live (153 MB peak RSS; entities 37.7 MB, tokens
44.5 MB [M 2, M 10]).

### 4.1 Deep copy per TU
A copy of every container.  27 ms per TU alone and 65 ms CPU with 16 at once
when the destination is fresh memory; 9 ms and 54-61 ms into buffers each
thread keeps (then 16 x 130 MB stay resident) [M 11].  Against the 2.4G
(about 230 ms) a TU saves that keeps 70-95 % of the gain.  Needs the complete
member list of 8 classes, kept complete for ever, and special cases for the
three pointerful containers and the pointers of 2.2.

### 4.2 Region snapshot at fixed addresses (the C declaration cache)
The C front end's state is 21 arrays and one heap; a snapshot is their used
bytes in 16 KB pages shared with the parent snapshot, pointers stored as
offsets, locations relative to a base; restoring "is a copy and one pass over
the location fields" [V driver/declaration-cache.cpp:14-21, 42-64].  It
transfers in idea (arrays of indices) but not in code: the C++ front end has
no single heap, about 200 containers instead of 21, and its restore would be
the copy of 4.1.  The location problem does transfer and is avoided by placing
the prefix before the main file (CS2).

### 4.3 Copy-on-write mappings in threads
The prefix's arrays in a memfd, each TU maps them `MAP_PRIVATE` with room to
grow.  No copying of what is only read; 5-10 ms alone, 22-55 ms CPU with 16
at once, because `mmap`/`munmap` and the faults of all jobs meet in one address
space [M 11].  Needs the same member list as 4.1 and a third storage mode in
`owned_array`.

### 4.4 Fork from a prefix process
A process analyses the prefix and forks per TU.  The whole state is private
and writable in the child at once, including everything not thought of: the C
preprocessor of the job, the driver's buffers, thread-locals, the allocator.
5-11 ms alone, **8.5-27 ms CPU with 16 at once** [M 11].  Reading costs
nothing (the page tables are copied), a write copies one page.  The child's
memory goes back to the system when it exits.  A TU that crashes takes only
itself.  vpp makes no threads for a `-c` [M 13], so a fork is safe there.
Costs: a process protocol; vccd is threaded and must not fork itself, so the
worker is a process it starts (it already starts programs,
[V server/spawn.cpp:1-10]); a child cannot give anything back to other TUs.
The removed first front end had this (`cxx_fork_tus`, `bench/cxx-fork.py`):
json's 70 TUs went from 31.5 s to 12.4 s CPU with it (doc/PLAN-cxx.md, Status
40 and 133).

### 4.5 Immutable base and per-TU overlay
The base is never written; every per-TU effect goes to an overlay.  It is what
`doc/RESEARCH-compile-server.md` concludes a compile server should have, and
the only design in which TUs share more than a prefix later.  This front end
is not built for it: entities are written in place by nearly every rule
(2.3), hash tables are part of the base, `mutable` caches are written by
`const` readers (2.2).  Every access to its arrays would have to choose base or overlay.  A rewrite of the analysis' data model, not a step.

### 4.6 Serialization (PCH-like)
The arrays are position-independent, so writing them to a file is close; the
pointers to string literals and `detail_texts`, and the objects of
`owned_objects`, need encoding.  It adds persistence over 4.3 and nothing
else; loading is 4.3.  Clang's own PCH brings its 23 TUs from 83.6G to 36.9G
plus 4.9G [M 6], the benchmark to beat.  Only interesting when a cold server
must start with a warm prefix; 0.45 s to rebuild it [M 2] says not yet.

### 4.7 Recommendation

**Fork from a prefix worker (4.4).**  It has the lowest restore cost measured,
the only one that does not get worse in a threaded address space, and it is
the only mechanism whose correctness does not depend on an inventory of a data
model of 145 000 lines that changes daily.  The work that remains is the same
for every mechanism (CS1-CS3, key, validation, fallback) plus about one file
of process handling.

Plan B, if D2 rules processes out: 4.1 first, then 4.3 for the largest
arrays.  Its extra steps: (B1) a generator, `src/tools/gen-snapshot-members.py`,
that lists every data member of the 8 classes from clang's AST into a checked-in
`.inc` with `--check` in CI, as `gen-syntax-views.py` does for the views, so a
new member cannot be forgotten; (B2) the three pointerful containers given a
clone (`source_files::entry` shared by reference count since a file's text is
immutable; `declared_names::scope` flattened into pooled arrays;
`detail_texts` shared); (B3) all lazily updated `record_chains` forced before
the base is frozen; (B4) a self-check in sanitizer builds: restore, then
compare every container with the original.  Expect 20-60 ms CPU per TU at 16
threads and either 2 GB resident or the page-fault cost of fresh memory
[M 11, M 12].

What would change this recommendation:
- vccd must stay one process, or macOS fork cost measures above about 30 ms
  for this state (D2): plan B.
- CS4 shows a TU writes most of the prefix's pages: then fork has no advantage
  over a copy into kept buffers except simplicity, which still decides.
- Sharing between TUs beyond the prefix (instantiations) becomes the next goal:
  then 4.5, and neither fork nor plan B is a step toward it.

## 5. Achievable speedup (question 4)

Instructions for all 23 TUs; seconds predicted at vpp's measured 10.5G
instructions per second (82.42G in 7.87 s [M 5]); clang++ `-O2 -c` is 24.77 s
[M 6].  Restore cost from M 11, an estimate until CS4.

| configuration | prefix once | rests | lowering, code | total | CPU | against clang++ |
|---|---|---|---|---|---|---|
| today | - | 70.96G | 11.46G | 82.4G | 7.9 s | 3.1x |
| system prefix (52 headers) | 2.94G | 17.24G | 12.25G | 32.4G | 3.1 s + 23 restores (0.2-0.6 s) | 6.7-7.5x |
| + project headers (CS10) | 4.00G | 3.93G | 15.19G | 23.1G | 2.2 s + restores (0.3-1.0 s) | 7.7-10x |
| + lowering that does not visit the prefix (CS8) | 4.00G | 3.93G | 11.46G | 19.4G | 1.85 s + restores | 9-12x |
| 30x | | | | 8.6G | 0.83 s | 30x |

`-fsyntax-only` against clang++'s 83.65G (16.2 s in the task statement):
system prefix 20.2G, 1.9 s, 8.4x; with project headers 7.9G, 0.76 s, 21x.

Wall time on 16 threads, cold [A, from M 12 scaled by instructions]: the
prefix is serial (0.3-0.45 s), the rests run in parallel; about 1.1 s against
clang++'s 4.45 s and today's 1.93 s on the loaded machine, 4x.  With the
worker already alive (a second build, a rebuild after an edit) the 0.45 s
disappear: about 0.7 s for all 23, and for one edited `.cpp` its rest and
code only (uci.cpp: about 1.6G + 0.9G, 0.25 s, against 1.5 s for
clang++ [M 6]).

**30x of clang++'s CPU on a cold build is not reachable with this plan.**
After every header is shared the backend (11.5G) and the `.cpp` files
themselves (3.9G) are 15.4G, 1.8 times the whole budget.  What would be
needed in addition: lowering and code generation about 3x cheaper (the code
duplicated between TUs is only 19 % [M 15], so reuse of generated functions
does not do it alone), the TUs' own instantiations shared across TUs (4.5),
and a restore under 5 ms.  What the plan does reach: about 7x with the system
prefix, about 10x with project headers.

## 6. Design

### 6.1 The prefix is a hint
Any list of headers is a correct prefix under 3.1: a header the list lacks is
simply analysed by the TU, one a TU does not need costs the worker time and
the TU the risks of 3.2.  So where the list comes from (D4) is separate from
everything else: given explicitly (`-fcxx-prefix=FILE`), or learned by vccd
from the dependencies of earlier compiles of the same configuration.

### 6.2 Front end: one stop, four phases (CS2)
```
pp.start_prefix(prefix)      built-in, command line, then the prefix file
parse.begin()                opens the translation unit's declaration-seq
parse.declarations()         until the token stream's end: the prefix's end
analysis.begin()             the predeclared typedef-names
analysis.declarations(0, n)
--- boundary: preprocessor has one inclusion, no conditional, default pragmas;
    parser at the top level; the transient pointers of 2.2 null ---
pp.enter_main(path)          the main file is opened and placed now
tokens.resume()
parse.declarations()         the tree grows: SyntaxContext reads its arrays again
parse.end()                  root
analysis.declarations(n, m); analysis.end()    instantiate_pending
lowering
```
- The preprocessor answers end-of-file at the end of the prefix file without
  reading the main file; the token stream's lookahead [V token-stream.cpp:75-115]
  then holds that end, not a token of the TU.
- The main file gets its locations after the prefix's files (today before
  [V preprocessing-file.cpp:148]).  This is done for every TU with `-include`,
  with or without a prefix, so there is one order.
- Without `-fcxx-prefix=` nothing stops and the phases run as today.

### 6.3 The worker (CS4, CS6)
A process of the vpp/vccd binary started with the job's arguments less input
and outputs.  It runs the driver up to the boundary, then serves requests:
`(main file, output, depfile and its target, stdout and stderr)`; for each it
forks; the child sets those in the job (`driver_job` derives them at
[V driver/job.cpp:600-640]; a function `driver_job_retarget` repeats exactly
those derivations), sets date and time, returns from the boundary and finishes
as any compile; its exit status is the job's.  The parent never continues past
the boundary.  It stays single-threaded.

### 6.4 Eligibility and fallback
A job goes the normal way (today's code, unchanged) when: it is not `-c`, `-S`
or `-fsyntax-only` of one C++ file; it has `-fcxx-dump=`, `-E`, `-H`, `-dM`,
`-M` alone, a module unit, standard input; no worker exists for its key and
none can be started (memory budget); the worker refused the prefix (errors in
it, not at the top level at its end, pragma state open, date or time
expanded); or the child reports "not transparent" (CS9) or any error
diagnostic: then the job is run again the normal way, so every error message a
user sees comes from the path without a prefix.  A TU that fell back is
remembered by path until its file changes.

### 6.5 Key, validation, invalidation
- Key (unconditional parts, compared exactly): the compiler's identity (a
  build id linked into the binary); the job's arguments in order, less the
  main file, `-o`, `-MF`, `-MT`, `-MQ`; the working directory; the environment
  variables the driver reads (`CPATH`, `CPLUS_INCLUDE_PATH`, `C_INCLUDE_PATH`,
  `SOURCE_DATE_EPOCH`, `VCC_*`); the bytes of the prefix list.  `-O` changes
  predefined macros and stays in the key.
- Recorded reads: every file the prefix opened (path as opened, device, inode,
  size, mtime in ns) and every path it probed and did not find (1 089 failed
  `stat`s for one TU [M 13]; a new file at one of them is the shadowing
  header).  The worker records them; the hooks are `source_files::open` and
  `preprocessor::stat_file` [V source-files.cpp:197-209; include-directive.cpp:32].
- When checked: at the start of each build generation
  ([V server/build-generation.cpp:1-16]) and before a job if the last check is
  older than a set time (D6).  About 2 200 `stat`s, a few ms.  A mismatch ends
  the worker; the next job starts a new one.
- A file changed within the timestamp granularity of the snapshot: the worker
  is not used for that build (ccache's rule, doc/RESEARCH-compile-server.md).
- Never shared: a prefix that expanded `__DATE__`, `__TIME__`, `__TIMESTAMP__`
  or `__BASE_FILE__` (the hook is `replace_dynamic`, [V cplusplus.h:776-786,
  1166]), or issued an error.

### 6.6 Dependencies
The child's `-MD` lists the main file, then the prefix list file and every
file of the prefix, then the TU's own: what `clang++ -include P -MD` writes
(CS3 compares).  D7 asks whether the TU's own reach should be written
instead.

### 6.7 Memory
A worker holds 153 MB for the system prefix, 220 MB with Stockfish's headers
[M 2, M 4].  A child adds the pages it writes while it lives [A: 15-40 MB;
CS4 measures `Private_Dirty`].  A budget (`VCC_CXX_PREFIX_MAX_MB`, default
1024) over all workers, least recently used ended first, and an idle time
after which a worker ends.  Nothing accumulates: unlike the threaded batch,
whose peak RSS is 2.83 GB for 23 TUs [M 12], a child's memory is gone at its
exit.

## 7. Steps

Each step is one or a few commits, verified as stated.  "Suites" means
`bench/cxx-run.py -j 6`, `bench/cxx-sema.py --check`, `bench/cxx-pp.py -j 6`,
`bench/vcc-diag.py -j 6`, `bench/cxx-depfile.py`, `bench/stockfish/run.py`,
and `bench/cxx-fe-bench.py --against` (budget +0.5 %).

### CS0. Measurements as a harness.  Status: measured by hand (section 1).
`bench/cxx-prefix.py measure [--project stockfish|json]`: builds the prefix
list from the TUs' `-H`, then M 1-M 8 and M 14 with today's `-include`: accept
or reject, defined symbols and sizes own against prefix for vpp and clang++,
the instruction split.  Files: the new script only.  This is the cheapest
experiment that could kill the approach and it has not: 23 of 23 and 71 of 71.
It stays as the regression test of 3.2 for every project added later.
Risk: none.

### CS1. The prefix must not reach the object.  Status: not started.
`eh_personality` is set once per TU by something of the prefix [M 7;
V frontend/c/ast-builder.cpp:383]: set it when a function with a landing pad
is emitted.  Files: `src/frontend/c/ast-builder.cpp`, `src/frontend/cplusplus/lower/exceptions.cpp`.
Verify: memory.cpp.o and movegen.cpp.o have the same symbols with and without
`-include prefix.h`; suites; `bench/vcc-eh.py`.  Independent of everything
else and right in itself.  Fallback: leave it, an unused undefined symbol.

### CS2. A stop between the prefix and the TU.  Status: done (2026-11-27).
Done differently in two places: CS2a only with `-fcxx-prefix=` (a placeholder
holds the main file's place on the include stack, so the normal path is
unchanged), and `allocation_names_spelled` is not kept pending: if its answer
changes after the stop the compile fails with that reason.  The boundary's
assumptions of 2.2 are not asserted.  `bench/cxx-prefix.py`: 1393 programs of
src/test/cxx/run, status, diagnostics and object as `-include` (C headers of
the test runtime as the prefix).
The largest front-end change, split so each part is byte-identical to before:
- CS2a. `preprocessor::start` in two: the main file is opened and placed after
  the command line's includes.  Files: `preprocessor/preprocessing-file.cpp`,
  `include/frontend/cplusplus/cplusplus.h`.  Verify: suites; objects of
  Stockfish byte-identical to before (`bench/stockfish/threads.py`), also for
  a TU with `-include`.  Risk: something compares locations with the main
  file's range, or assumes the main file is `inclusions[0]` from the start:
  `warn_unused_macros` runs when `main_file` is left
  [V preprocessing-file.cpp:232-238], `__INCLUDE_LEVEL__` is computed from the
  stack [V ext/gnu/predefined.cpp:38; its formula not read].
- CS2b. `parser::translation_unit` as begin / declarations / end; the list of
  top-level declarations without `root`.  Files: `basic/translation-unit.cpp`,
  `declaration/simple-declaration.cpp`, `parse/syntax-tree.cpp`, `parse.h`.
  Verify: `bench/cxx-syntax-conformance.py`, suites.  Module units keep the
  one-piece path.
- CS2c. `semantic_analysis::translation_unit` as begin / declarations(from,
  to) / end; `SyntaxContext` re-reads the tree's arrays after the tree grew.
  Files: `sema/semantic-analysis.cpp`, `syntax.h`, `sema.h`, `lower/compile.cpp`.
  Verify: suites byte-identical when called as one range.
- CS2d. `-fcxx-prefix=FILE`: the stop itself, in one process: prefix parsed
  and analysed, then the rest.  The boundary's conditions are checked and
  assert the assumptions of 2.2 (transient pointers null, parser flags at
  rest).  `allocation_names_spelled()` must give the answer of the whole TU:
  count as tokens are made instead of scanning, and keep a refusal that depends
  on a later `operator new` pending until the end
  [V basic/allocation-functions.cpp:206-222].
  Verify: for every test of `src/test/cxx/run` and every Stockfish TU,
  `vpp -fcxx-prefix=P` and `vpp -include P` agree: same diagnostics, same
  defined symbols and sizes, the programs' output equal; the objects are
  byte-identical if identifier numbers do not reach the output [A].  If they
  do, `-include P` takes the same two-part order and the comparison is exact
  again.  New harness mode `bench/cxx-prefix.py stop`.
  Risk: an analysis rule that reads the tree or the tokens beyond its own
  declaration.  One is known (above); `grep` found the tree's size and root
  read only by the parser and the dumps.  Fallback: none needed, the option is
  off by default.

### CS3. The meaning of the prefix against clang.  Status: partly (2026-11-27).
`bench/stockfish/prefix.py`: the 23 objects byte-identical to `-include`, and
the defined symbols of the TU with its own includes at -O2 (at -O0
position.cpp's and uci.cpp's differ; not looked into).  Not done: clang's
depfile with a prefix, the node count of a linked binary.
`-fcxx-prefix=P` accepted by the driver and the harnesses: `bench/cxx-run.py
--prefix`, `bench/cxx-depfile.py --prefix` (the depfile of 6.6 against
`clang++ -include P -MD`), `bench/stockfish/run.py --prefix` (the `bench` node
count equals clang++'s).  Files: `driver/cmdline.cpp`, `lower/compile.cpp`,
the three scripts.  No sharing yet: every TU still analyses the prefix.  After
this step the meaning is fixed and tested, and everything later must be
byte-identical to it.

### CS4. Fork at the boundary: the experiment.  Status: done (2026-11-27).
`vpp --cxx-prefix-batch`, driver_environment::cxx_boundary.  All 23 objects as
CS3's at -O0 and -O2, one child at a time and 16, also under ASan+UBSan.
- Instructions (-O2): 82.4G own includes, 37.2G forked.  The prediction of
  32.4G missed 22 x 0.22G: each child does the end-of-TU analysis and lowering
  of the prefix's declarations, which section 5 counted once (CS8 is part).
  For memory.cpp, parent and child are exactly `-include`'s instructions.
- CPU, back to back at load 7 (3 runs): clang++ 14.6-16.1 s, forked 3.8, 4.0,
  5.4 s: about 4x, not the 7x predicted (section 5 used a clang++ time of
  24.8 s taken under load).  Wall 1.5 s with -j 16 at load 27.
- 1.1 M minor faults, 1.0 s of system time in the 4.7 s of one run (the 23
  processes without a prefix: 1.4 M faults, 1.7 s).
- Not measured: a child's Private_Dirty, fork latency, macOS.
**The gate (under 4 s CPU) is met only in the best runs.**  CS5-CS7 wait for
the user's decision.
`vpp --cxx-prefix-batch JOBS -j N`: one process takes the first job to the
boundary, then forks a child per job, N at a time; the child retargets the
job (6.3) and finishes.  Files: `src/bin/vpp/vpp.cpp`, `src/driver/job.cpp`
(`driver_job_retarget`), `lower/compile.cpp` (a boundary callback),
`bench/stockfish/prefix.py`.  About 200 lines.
Verify: every object byte-identical to CS3's (`cmp`); under ASan.
Measure and record here: instructions and CPU for the 23 TUs (prediction:
32.4G, 3.1 s + restores), `Private_Dirty` of a child at exit, fork to first
instruction, wall at `-j 16`, and the same numbers on macOS.
**This step decides.**  If the total is not under 4 s CPU, or a child dirties
most of the worker, stop and report before building CS5-CS7.
Risk: something of the job depends on the main file before the boundary
(debug name, `-MT` default, `__BASE_FILE__`): CS2d's comparison with a
*different* main file per TU finds it.

**The work each child repeats** (looked at 2026-11-27, nothing kept).  A
child compiling an empty file runs 0.32G instructions beyond the parent.
callgrind (its child stops in code generation, so these are of the front end):
`instantiate_pending` 192M, lowering 91M of which mangling 57M.
- Draining the wanted work before the fork (`instantiate_pending` keeping its
  queues): 37.2G to 33.1G, 3.5 s CPU, but the objects are no longer
  `-include`'s: 4 TUs differ in symbol sizes at -O0 and -O2.  Of its parts,
  `postponed_bodies` and `pending_definitions` cause it (the other two change
  nothing and save nothing).  A body instantiated early is compiled
  differently (`allocation_result<char*>`'s constructor spills its parameters).
  Why is not known; until it is, nothing is drained at the boundary.
- No symbol for a function only declared (CS8's first part): 37.2G to 37.0G,
  and objects change.  Not worth it.

### CS5. Key and validation.  Status: not started.
The recorded reads of 6.5 in the worker, the check, and the refusals (date,
time, errors, open state).  Files: `preprocessor/include-directive.cpp`,
`lexer/source-files.cpp` (two hooks), a new `src/driver/cxx-prefix.cpp`.
Verify: `bench/cxx-prefix.py invalidate`: a prefix header edited, replaced
(new inode), touched; a new header placed earlier on the search path; a `-D`
changed; the compiler rebuilt: each time the next compile equals a compile
without a worker.  The cases come from `bench/stockfish/cache.py`'s list of
what it does not cover.

### CS6. The worker in vccd.  Status: not started.
vccd starts a worker per key, sends jobs, waits in a blocking region
(`vtask`), applies the budget and idle time of 6.7, ends workers at a
generation whose validation fails, falls back per 6.4.  Files:
`src/server/compile.cpp`, a new `src/server/cxx-prefix-worker.cpp`,
`src/server/spawn.cpp` (start without the request's stdio),
`src/server/build-generation.cpp`.  Verify: `bench/stockfish/vccd.py` with the
prefix: objects byte-identical to CS3's over three builds;
`bench/vccd-test.py` gains: worker killed mid-build, a job cancelled, budget
of one worker with two keys, the server restarted; vccd under TSan
(`bench/vccd-cc.py`) since the server side is threaded.  Risk: descriptors of
other requests leaking into workers (spawn.cpp passes only 0-2 for that
reason); a child outliving a cancelled request.

### CS7. Where the list comes from.  Status: not started; D4.
vccd keeps, per key without the prefix, the system headers of the depfiles of
the compiles it ran, and uses their union, sorted, as the next build's
prefix; a header whose inclusion alone fails is dropped and remembered.
Files: `src/server/cxx-prefix-worker.cpp`.  Verify: Stockfish cold twice
through vccd: the first build without a prefix, the second with all 52
headers; objects as CS3 with that list.  Until then the list is explicit.

### CS8. Lowering that does not visit the prefix.  Status: not started; its
cheapest part measured under CS4 (0.4 %).
Symbols declared when first used, not for every declared name
[V lower/definitions.cpp:334-392]; the vtable loop over all entities
[V :430] over the dynamic classes only; the linear `done` search [V :393, 411-415]
a flag.  Worth 0.8G on the system prefix and 3.7G with project headers
[M 5].  Verify: objects byte-identical before and after on the suites and
Stockfish, or, if the order of symbol creation changes labels, equal by
symbols and sizes with `bench/stockfish/run.py`'s node count.  Independent of
the prefix; can be done first.

### CS9. Transparency: detect and fall back.  Status: not started; D3.
With the include graph the worker records (which file's text each token of
the prefix is from, which files each file includes), a child knows the files
its TU's own includes reach, growing as its `#include`s are met, and reports
"not transparent" when its own text
(a) defines or undefines a macro whose name the prefix spells, before an
include of a prefix file;
(b) uses or tests a macro defined in a file it has not reached;
(c) includes a prefix file inside a scope or with pragma state open;
(d) names an entity, selects an overload or a specialization, or requires
complete a class, whose declaration (for completeness: definition) is in a
file it has not reached;
and the worker refuses a prefix with a must-emit entity (3.2 #5).
Files: `preprocessor/macro-replacement.cpp`, `include-directive.cpp`,
`basic/name-lookup.cpp`, `overload/overload-resolution.cpp`,
`template/specializations.cpp`, the completeness check.  Verify:
`src/test/cxx/prefix/`: one TU per row of 3.2 that must fall back, and its
output then equals the compile without a prefix; Stockfish and json: no TU
falls back (if one does, it is a finding about the TU or about the check).
Risk: (d) has many paths; a missed one means `-include` meaning for that TU,
which CS0's symbol comparison on real projects bounds.  Lookups from
instantiations at the end of the TU are checked against the TU's whole reach.

### CS10. Project headers.  Status: not started; needs CS9.
A second worker forked from the first: the system prefix, then the project's
headers; ended and forked again from the first when a project header
changes (1.06G, 0.1 s).  Requires: the must-emit filter of 3.2 #5 in place of
a refusal; M 8's `UCIEngine::move` difference explained; headers that cannot
be combined detected (a clash is an error in the prefix: drop the header).
Verify: CS0's comparison on prefix2.h shows no symbol difference; node count.
Gain: rests 17.2G to 3.9G [M 3, M 4].

### CS11. Record the result.  Status: not started.
`bench/stockfish/prefix.py` in `AGENTS.md`'s list, the numbers of CS4, CS8 and
CS10 in this file and in `doc/PLAN-cxx.md`'s 30x table; `ci/premerge.sh` gets
`bench/cxx-prefix.py stop` on `src/test/cxx/run` (needs only the checkout).

Order: CS0, CS1 and CS8 are independent and safe.  CS2, CS3, CS4 are the
spine; stop after CS4 if its numbers disappoint.  CS5-CS7 make it a product,
CS9-CS10 make it transparent and extend it.

## 8. Decisions for the user

- **D1. What "30x" measures.**  Recommendation: state two targets.  CPU of a
  cold build against clang++ `-O2`: aim for 10x with this plan (7x at CS4,
  10-12x at CS10) and treat 30x as needing a separate backend plan.  Wall time
  of a rebuild after one edited `.cpp` in a warm server: aim for 10x
  at CS6 and measure whether 30x holds for the small TUs.
- **D2. May vccd use worker processes for C++?**  Recommendation: yes (4.7).
  If no: plan B, about three times the work and two to five times the restore
  cost.  Needs one measurement on macOS in CS4 either way.
- **D3. Explicit or transparent.**  Explicit means the build asks for a prefix
  and gets `-include` meaning, as with any PCH.  Transparent means vccd adds
  it and CS9 guarantees the TU's own meaning or falls back.  Recommendation:
  explicit until CS9 is done and has run on three projects without a miss;
  the Stockfish numbers are quoted as "with a prefix" until then.
- **D4. Where the list comes from.**  Recommendation: explicit file first
  (CS3), learned by vccd later (CS7).  A lexical scan of `#include` lines
  before the first compile was considered and rejected: it sees `<windows.h>`
  and `<arm_neon.h>` behind `#if`.
- **D5. What "verified" means against a build without a prefix.**  Objects
  cannot be byte-identical (3.2 #4), for clang neither.  Recommendation:
  byte-identical between sharing and not sharing the same prefix (CS4 against
  CS3), and, against no prefix, equal defined symbols and sizes plus the
  programs' behaviour.  Making the object independent of declaration order
  (labels numbered per function, functions emitted in a canonical order) would
  restore byte identity and departs from clang's order; not recommended now.
- **D6. How often the files are checked.**  Recommendation: once per build
  generation and before a job when the last check is older than 2 s.
- **D7. `-MD` in prefix mode.**  Recommendation: all files of the prefix, as
  clang with `-include`; the TU's own reach only once CS9 exists.
- **D8. Diagnostics.**  Recommendation: any error sends the TU through the
  normal path, so error text never depends on the prefix; warnings are
  printed from the prefix path as they are.
- **D9. Project headers (CS10).**  Worth 13.3G of the 17.2G that remain, and
  the part with real semantic differences today [M 8].  Recommendation: only
  after CS9.

## 9. Assumptions still open

| assumption | checked by |
|---|---|
| no writable global besides the thread-locals of 2.2 | irrelevant for fork; plan B: TSan on the batch |
| transient pointers and parser flags are at rest between top-level declarations | CS2d asserts |
| no analysis rule but `allocation_names_spelled` reads beyond its declaration | CS2d's comparison |
| identifier numbers do not reach the object | CS2d |
| a child writes 10-30 % of the worker's pages; fork of the real process costs what M 11 shows | CS4 |
| macOS: fork cost, 16 KB pages | CS4 |
| libc++'s headers analysed in one order mean the same as in each TU's order | CS0 on each project; CS9 (a), (b) |
| wall-time predictions of section 5 | CS4, CS6 |
