# Unsupported declarations to zero: the step log

Every step's status, decisions and findings, oldest first (moved from
doc/PLAN-cxx-unsupported.md on 2026-11-23; new entries go here).


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
- Batch 1.2 (db25d4b23, no simplify: the scan for another type specifier has
  no existing helper to reuse): C++98 `auto` alone is the placeholder, as
  clang's extension.  Unsupported 39674 -> 39660; we accept +1.  The 488
  refusals of a forced -std=c++98 sweep were 14 counted declarations: the
  harness counts its own RUN lines only.  Rule from here: size a cause with
  --why (counted declarations), never with a free sweep.
- Batch 1.3 (dropped once, no commit): [dcl.fct] parameter-declaration-clause.cpp:474
  is 446 dynamic-exception-specifications (`throw(T...)`), 39 attributes, 22
  bare tokens.  The agent's patch (-374 unsupported) raised lowering refuted
  +3 and we reject +1, so it reverted; the patch is kept for 1.3b.
  Also f7af67627 (orchestrator): a redeclaration of a held name says why
  ([basic.scope.declarative], 504 in one opaque key, now followed).
- 1.3a (8e751f1a3, orchestrator): __is_array/__is_bounded_array false for
  T[0]; __is_const/__is_volatile read an array's element cv.  We reject 679
  -> 667, types +932, constants +928, unsupported +4 (wrong rejections now
  honest refusals).  No simplify needed (element_type() reused).
- 1.3b (269726ab5, with the prerequisites 77d21320a, b43fa23f3, 1138c459d;
  the agent's one commit split by the orchestrator, one fix each, every
  intermediate builds alone): dynamic-exception-specifications analysed.
  The lowering refutes it exposed were two lowering bugs (an array element's
  constructor without the construct's arguments; a constructor's class
  argument not a caller-destroyed temporary) and __is_convertible counting a
  private copy constructor.  Unsupported 39664 -> 39298, we reject 667 ->
  660, lowering refuted 34 -> 23, lowered +28, constants +145, known
  failures 873 -> 861; we accept +11 (not identified yet: queue).
  special/class.dtor/p3-0x.cpp is a new known failure: no typeinfo for
  classes with a vtable (S7), hidden before.  The agent ran past its limit
  with work left in the background; stopped by the orchestrator.
- 1.4 (5d4a09877 fix, b0766d326 simplify): ext_vector_type(N) on a typedef
  (either position), through the alias path's code; only the typedef path
  opts in.  Unsupported 39298 -> 39155, agree +153, types +567, no refuted
  or we reject moved.  Left: bool vectors, vector values ([ext.clang] rose
  1302 -> 1419: those declarations stop at vector expressions now).  Also
  c5b744d0d: --why keeps an extension's name in its key.
- Batch 1 done: 39709 -> 39155 (-554).  Lessons: size with --why only;
  brief "an exposed unrelated bug: report and stop", so a commit holds one
  fix.
- Batch 2 starts (list regenerated after batch 1).  abcaed41c (orchestrator):
  a template-id in an expression naming no function template says why
  (677 in one opaque key, now followed to the holders).
- 2.1 (87bf3f41c, no simplify needed): a variable template without an
  initializer is a definition, through variable_template_definition
  (`extern` ones stay refused there, checked).  The [temp.class] fallback
  names the shape: identifier 323, qualified-id 150, declarator 93,
  noptr-declarator 28, simple-template-id 27, decl-specifier-seq 15.
  Unsupported 39155 -> 39027, agree +131.
- 2.2 (f3f27a905, no simplify needed): a class template's static data
  member defined outside it without an initializer, through
  member_definition_of_template.  Unsupported 39027 -> 38958, agree +66; we
  accept +6.  The harness now writes work/cxx-sema-accepted.txt (each
  we-accept declaration): the 6 are one missing check, the default
  initialization of such a member on instantiation ([dcl.init]/7, clang "no
  matching constructor"): queue.  Rule from here: a step that raises we
  accept names the new lines (diff of that file) in its report.
- 2.3 (dropped once: we reject +2 from a missing access exemption) ->
  2.3a (03a5ad9ab fix, 30bc2de77 simplify): no access check on the names of
  an explicit specialization's or instantiation's declaration
  ([temp.spec.general]/6; one flag, accessible() consults it): we reject
  660 -> 613.  Then 9dfa80a4b (orchestrator): the same for a partial
  specialization's template arguments ([temp.class.spec]/10), and
  c05b4d3ce: non-type parameters of pointer-to-member, array, function type
  (the agent's patch; its `?:` rewritten, caught by the hook).  Together
  unsupported 38958 -> 38802, we reject 660 -> 606.  We accept +10, all
  part.spec.cpp: a specialization's base-clause naming a private member is
  not checked, because that base-clause is not analysed (queue).
- 2.4 (57121f77b fix, then simplify: not_supported_because): an explicit
  specialization of a class template's static data member with an
  initializer, and its explicit instantiation.  Unsupported 38802 -> 38797
  only: named_specialization's refusals now name the shape: declarator
  neither noptr nor ptr 322 (temp.explicit) + 204 (temp.expl.spec), no
  declarator (`template<> struct N::B<int>;`) 195, no function template
  for the declarator-id 139, a function with a named declarator-id 120.
  The blocker the agent found: a class instantiation defines every member
  at once (instantiation.cpp ~536), so an explicit specialization of a
  member cannot be recorded before the template's definition runs.  That
  is the design note's [temp.inst]/2 step: next.
- Batch 2 done: 39155 -> 38797 (-358), we reject 660 -> 606.
- Step design, [temp.inst]/2-4 for out-of-class member definitions (batch
  3.1): define_class_instantiation defines each out-of-class member
  definition of the template (template_member_definitions,
  define_template_member) when the class is instantiated.  In-class member
  bodies already wait: postponed_bodies, defined by instantiate_pending()
  after the translation unit's declarations ([temp.point]/7) only when
  member_definition_needed or explicitly instantiated.  The step records the
  out-of-class ones the same way (one queue, the same condition; a static
  data member needed by odr-use, [temp.inst]/4) instead of defining them at
  once, and an explicit specialization of the member recorded before
  instantiate_pending() replaces the template's definition
  ([temp.expl.spec]/7).  No second mechanism.
- 3.1 (2785a3a2f fix, 7b0c3bdf9 simplify): out-of-class member definitions
  of a class template specialization wait in postponed_bodies and are
  defined by instantiate_pending() when a member they may define is needed
  (or virtual, or explicitly instantiated); const/constexpr static members,
  member templates and nested classes still with the class.  An explicit
  specialization drops the waiting definition of that member.  Unsupported
  38797 -> 38761, we reject and lowering unchanged; we accept +1
  (p2-20.cpp:141: `static` on an out-of-class member definition was only
  reported at the eager instantiation: needs a check at the definition).
  Simplify: a waiting definition with no name token (an operator's) matched
  any member name.  Queue: that storage-class check; the no-initializer
  explicit specialization refusal (`template<> int C<int>::y;`) can be
  lifted now; an overloaded member function's specialization does not drop
  the waiting definition.
- 3.2: 2f34f89b0 (no simplify needed): `static` on a class template's
  out-of-class static member definition is an error at the definition (we
  accept 4314 -> 4313).  Fix 1 (`template<> int C<int>::y;`) dropped once:
  -7 unsupported but lookup refuted +3, because the specialization of a
  member of an implicit instantiation reuses the member's entity and the
  lookup claim names its first declaration, the in-class one, where clang
  names the specialization (patch in the scratchpad: b32-fix1.patch; to
  retry with that fixed first).  Fix 3 (an overload's specialization) not
  started.  The template micro-steps give little now: back to the largest
  keys of the work list.
- 4.1 (9631334c0, no simplify needed): a non-static data member with a
  placeholder is ill-formed, as clang (-4).  The agent took the 676 key as
  stale (its own instrumentation had moved the line); it was not.  The
  orchestrator gave the refusal a context detail: a declaration that takes
  none 350 (alias-declaration, template default argument: clang errors;
  `template<auto x>`: allowed, C++17), a type-id 273, a parameter 53.
- 4.2 (dropped once: we reject +1 from typeid) -> 28bb429d7
  (orchestrator): typeid needs std::type_info declared, not complete (we
  reject 606 -> 573), then the agent's patch: auto in a type-id with a
  trailing return type (-16; types/constants verified -13/-15 because
  cwg12xx.cpp's g() is now ill-formed as clang says, checked by a
  per-declaration diff of the old and new vpp).  We accept +1
  (expr.prim.lambda/templates.cpp:77: a by-value Boom<T> parameter does not
  instantiate its ill-formed copy constructor: queue).
- 4.3 (2d2f5a1a9, no simplify needed): a template declaration whose
  declarator has a trailing return type (syntax kind `declarator`) is a
  function template's; 2.1 had sent that kind to the variable-template path,
  where no placeholder is allowed.  Unsupported 38765 -> 38679 (-86), agree
  +80; we accept +6: substitution failures in a function template's
  trailing return type are not analysed at the use (queue).  What is left
  of the key: variable templates with a placeholder (~270, clang accepts:
  deduce per instantiation).
- 4.4 (983a8e60d, no simplify needed: two lines): a variable template with
  a placeholder and an initializer; each specialization deduces through
  simple_declaration.  Unsupported 38679 -> 38558 (-121), agree +115.  We
  accept +13 (6 places): clang checks a variable template's dependent
  initializer in the definition (unexpanded packs, the template in its own
  initializer, a bad lambda default argument); vcc analyses it only per
  specialization (queue: the initializer's definition-time checks,
  [temp.res]).  types refuted +1, accepted as a clang quirk (decision):
  SemaCXX/decltype.cpp:260 (GH161196, a crash test) where clang leaves
  decltype(B<int>::XBitMask<0>) as the undeduced `const auto` (it even
  reports the specialization's type "depends on itself" when asked), and
  vcc deduces const int as [dcl.type.auto.deduct] gives.  Listed in
  clang-failures.txt.
- 2026-11-23 rebase on origin/main 12fd44be6 (user's request; 40 commits,
  #550 among them).  Commits replayed one by one; dropped: step 2's
  cxx-run cache (03a8f1150, f1f1d92cf: #550 has the same cache).
  Resolved: --why's second pass in main's process pool; step 1b's
  evaluation detail in main's evaluate_value/evaluate_constant split;
  batch 2.4 and 3.1 in main's overload_of_type/explicit_instantiation_target;
  d9b222d5a: 2.4's static data member explicit instantiation definition
  moved into main's member_explicit_instantiation (its own branch was no
  longer reached).  Metrics re-blessed against main's: unsupported 39530
  -> 38368, we reject 677 -> 569, lowering refuted 36 -> 25.  Backup of
  the old branch: tag backup/cxx-unsupported-zero-pre-rebase (local).
- Next (from the --why of a1abbbf94): the parser's later-step grammar:
  __declspec 320, a ( in a noptr-declarator 297, calling conventions 65.
- 5.1: the agent's __declspec parser patch cost +0.83 % in fe-bench from one
  extension-table row.  Cause (orchestrator, perf): parser::parser matched
  every lexer keyword entry against every keyword row by strcmp, per TU,
  half of a small TU's instructions.  2628a6774: a hash lookup instead,
  fe-bench -36.9 % against main.  Then f61c9c9d5, __declspec parsed
  (grammar.inc: microsoft-declspec, as cxx-spec-refs wants): not parsed
  -1223, unsupported +994, together -229.  Decision: a parser step is
  measured by unsupported + not parsed.  We accept +5 -2 (named in the
  commit).  Also: cxx-spec-refs found six later-draft labels from earlier
  steps ([temp.spec.general], [dcl.spec.auto.general]): fixed; cxx-spec-refs
  joins the per-step gates (under 1 s).  Next: __declspec's attributes in
  the analysis (dllimport/dllexport, selectany, thread, uuid, align,
  property).
- 5.2 (89017527f, no simplify needed: table rows): __declspec dllexport,
  novtable, restrict, noalias, allocator accepted with no effect, as clang
  on this target.  Unsupported 39362 -> 39051.  dllimport left out: the
  harness's clang runs for aarch64 Linux (the test's -triple is dropped,
  cxx-pp.py VALUE_DROP), where clang ignores the attribute ("not
  supported") yet still makes `__declspec(dllimport) int x;` a declaration
  (`U x`); vcc must do the same (5.3).
- 5.3 (dropped once: we reject +1) -> 64d6a8cd3 (orchestrator): a constexpr
  static data member redeclared at namespace scope needs no initializer
  (C++11/14 definition, C++17 redundant redeclaration; we reject -2), then
  5462949be, the agent's dllimport: a variable declaration carrying it and
  no initializer is no definition, as clang on its target.  Unsupported
  39051 -> 38677, verified +505 in all.  Queue: clang emits a constexpr
  static member's inline variable for its namespace-scope redeclaration,
  vcc does not.
- 6.1 (26957ac63, simplify after): a call with no arguments to a name no
  scope has is ill-formed ("use of undeclared identifier"), as clang (-20).
  Kept conservative by two workarounds (no name beginning with `_`; off
  once a declaration of the TU was not parsed), because "no scope has it"
  is not reliable: names the analysis does not enter (enumerators of an
  enumeration with a fixed underlying type, anonymous-union members, the
  names of a declaration not parsed) leave their scope marked as known
  (making non-call names ill-formed gave we reject +156).  Next (6.2):
  mark those scopes unknown, or enter the names; then the workarounds
  go.  ADL for calls with arguments: template arguments' associated
  namespaces, function and member pointers, overload sets are missing.
- Decision (2026-11-23, from the user): each agent has 25 minutes (soft
  limit 20: stop coding, run the gates).
- 6.2 (dropped: marking scopes unknown only loosens 6.1's rule and costs
  agreement).  Its finding, the names lookup misses although they are
  declared (each a lookup bug, to fix by entering the name, one commit
  each): a parameter's name in a later parameter-declaration
  ([basic.scope.param]: `int n, int v[n]`, `T x, decltype(x) y`); `using
  enum` enters no enumerators ([enum.udecl]); an anonymous union's members
  at namespace scope ([class.union.anon]); a lambda's parameter in a later
  parameter; the body of an extern "C" function redeclaring one of another
  namespace looks up in that declaration's scope, not its own
  (cwg3xx.cpp:1037); GNU attribute arguments (enable_if) not in scope; a
  friend's noexcept in a complete-class context ([class.mem]/7); enumerators
  of a refused enum-specifier not held.  Patch with the scope marks:
  scratchpad unknown-scopes-b62.patch.
- 6.3 (eec81e142, a031b5924, 31364413f; simplify after): a parameter's
  name in later parameters, a lambda's parameters in its trailing return
  type ([basic.scope.param]/1), using enum of a scoped enumeration
  ([enum.udecl]/2).  Unsupported 38657 -> 38639, we reject -1, we accept +1
  (canonical-expr-type.cpp:12: equivalent dependent expressions in function
  template signatures, [temp.over.link], not checked: queue).  Left of
  6.2's list: anonymous union members at namespace scope, extern "C"
  redeclaration bodies, GNU attribute arguments, a friend's noexcept,
  enumerators of a refused enum-specifier.
- 7.1 (dropped twice): an instantiation found ill-formed makes the use
  ill-formed ([temp.inst]/1).  First drop: we reject +274, mostly a harness
  artifact: clang's error is at the template's line, the use only has
  "in instantiation of ... requested here".  f950839a5 (orchestrator): the
  harness counts such a note (of an error's) as the use's rejection; the
  honest baseline: agree -1032, we accept +1037 (declarations clang
  rejects through an instantiation vcc misses, counted as agreement
  before).  Second drop: with that, agree +172, unsupported -295, but we
  reject +123: a non-dependent error in the template reported again at the
  use, and dependent rules checked too early (dcl.constexpr/p6.cpp:60).
  Decision: split, not hard: 7.2 works through those rejections by cause.
  Patch: scratchpad b71.patch, the new failures b71-new-after-harness.txt.
- 7.2 (dropped: we reject +110 even with a fix for the non-dependent
  errors): the causes, counted: eager instantiation where no complete type
  is needed (~40: extern S<int> *p; a template argument; a parameter type;
  a pointer), a non-dependent error inside the template (13: a heuristic
  patch saved), a template-time check vcc does late (14), dependent rules
  vcc checks and clang does not (~15).  Patches: scratchpad
  b72-independent-error.patch.
- Step design, [temp.inst]/2 (the design note's first item, deferred until
  now): a class template specialization is defined only where a complete
  type is needed.  Today template_type (declared-templates.cpp) instantiates
  every template-id; a lazy path exists for member return types
  (lazy_return, complete_instantiation), and only three places complete a
  lazy class (name-lookup, base-clause, instantiation).  7.3a: one helper,
  require_complete(qualified_type) (through arrays and cv), called at every
  place that reads a class's completeness, size, members or bases; with
  classes still eager no metric moves (a pure refactor).  7.3b: template_type
  declares the specialization (lazy) unless the context needs it complete,
  and the uses complete it.  Then 7.1's propagation again.
- 7.3a (edca5283c, no simplify: tried making require_complete non-const;
  the const_cast it holds is the one that size_of_type and
  class_member_lookup each had inline before, so one place is better):
  require_complete(type or class) at ~45 sites that read a class's
  completeness or size; metrics unchanged.  Not yet audited: reads of
  members, bases, constructors, destructors or the vtable without a .size
  check, and references into type_table or entity_table taken before a
  call (a lazy completion may grow the tables): 7.3b re-checks them.
- 7.3b: 25f9fbd3d, the completion sites 7.3a missed (metrics unchanged,
  two crashes avoided: a type_table reference held across a completion).
  The lazy line itself (scratchpad lazy-template-type-on-top.patch):
  unsupported -353, agree +293, lowered +7, but we reject +8 (five newly
  analysed bugs: CTAD's deduced type against A<int> twice,
  __is_same(common_type_base<char>, ...), warn-consumed-analysis.cpp:413,
  align-x86.cpp alignments) and we accept +61 (explicit instantiations of
  ill-formed specializations judged well-formed).  Then a fix of 7.3a's
  garbled if in name-lookup.cpp (missed in its review: read every changed
  condition) and copied arguments for re-instantiation.
- 7.3c (eac5a6835, c666658e3, bd26f9148, 726bd4d2b, 5232bb7cb, e7b397359;
  each intermediate commit builds, checked): the five bugs, then the lazy
  line: a class template specialization named where no complete type is
  needed is declared, not defined ([temp.inst]/2).  Unsupported 38639 ->
  38327, we reject 561 -> 524, agree +288, types +181; we accept +61
  (explicit instantiations of, and members of, specializations only named
  that clang finds ill-formed: queue).  No simplify needed (conditions
  read).  Next: 7.1's propagation again (7.4).
- 7.4 (67df18202 harness, 2540234e9, ffbdbd6fa; propagation not landed):
  the 'while substituting/checking ... required here' notes also mark the
  use rejected (agree -12, we accept +12, same vpp); a specialization's
  ill-formed member class does not make it ill-formed ([temp.inst]/3); a
  member function's dynamic exception specification is not analysed with
  the class from C++11 ([temp.inst]/15).  The propagation's we reject rise
  is down from +27 to +7: deferred overload resolution with deduced return
  types (overload-resolution-deferred-templates.cpp:133, 2), a template-time
  check made at instantiation (p15-cxx0x.cpp:97, 2), a non-dependent error
  reported again at a local class's use (instantiate-local-class.cpp:191, 2),
  and a test clang crashes on (1).
- 676011abc (orchestrator): a run where clang crashes (a signal: 18 tests,
  33 runs) is skipped; decision: a crashed reference is no reference.
  Counts drop with those runs (unsupported -677, we reject -29).
- Decision (2026-11-23, from the user): push the branch to the PR after
  each commit lands, not only at a step's end.
- 7.5 (dropped): the propagation's we reject rise is +6 on the new
  harness, +4 with a fix for the local-class case (scratchpad
  b75-prop-independent.patch); its gain is only -46 unsupported now.
  Marked hard (parked), revisit when nothing else is left.  Found: a
  correctness bug, two entities for one specialization when an argument is
  itself a new lazy specialization (p15-cxx0x.cpp:97; scratchpad mp5.cpp:
  `ap<places<0>,places<0,0>>::type` resolves to a never-defined second
  record): queue, its own step.
- 8.1 (c77c30198, dd3cc323a, 2e7778717, ee8385b36; no simplify needed):
  clang's type traits: __is_literal of a union, __has_unique_object_
  representations (void, functions, references, _BitInt, classes with
  reference members), __is_layout_compatible (new: scalars, enums, arrays,
  standard-layout classes without bases), __is_complete_type (new).
  Unsupported 37643 -> 37344 (-299), we reject and we accept unchanged.
  Left: implicit lifetime, standard layout with bit-fields, unions and
  bases in layout compatibility, bitwise_cloneable, trivial_abi.
- 8.2 (5b7c6edcc, 821c9f313, 2d7eb1d51; no simplify needed: table rows
  and constinit's check reused): x86 attributes clang ignores on aarch64;
  attributes with no effect on code (optnone, consumed analysis,
  reentrant_capability, preferred_type); require_constant_initialization
  as constinit's check.  Unsupported 37344 -> 37047 (-297), we reject and
  we accept unchanged.  Queue: pass_object_size raised refuted claims in
  files that do not use it (bitfield.cpp, class-layout.cpp:658): a flag
  collision?; unavailable, init_priority, multiversioning, address_space,
  code_seg/uuid checks, swiftcall's checks.
- 8.3 (0a8827707, no simplify needed: one rule for the family): C23's
  __builtin_stdc_* bit functions and __builtin_bitreverseg, analysed and
  constant-evaluated; the rotations lowered, the others refused in
  lowering with a reason.  Unsupported 37047 -> 36738 (-309).  Left:
  __builtin_is_within_lifetime (evaluator), the synthesizes_from_spaceship
  traits (rewritten candidates), 128-bit results (integer_wrap keeps 64).
- 8.4 (afbedae83): a namespace head's attributes that change nothing
  (-41).  The agent stopped after 3 minutes; the big sites go to 8.5.
- 96ccf613f (orchestrator): an object of a lazily declared specialization
  whose definition failed carries its instantiation's refusal ([class.pre]
  509 -> 120 in --why).
- 8.5 (dropped once: we reject +2) -> 650420038 (orchestrator): alignof
  of a variable reads the variable's alignment; then 00e48571f, the agent's
  declarator alignas ([dcl.decl]/4, [dcl.align]/1, /5).  Unsupported
  36697 -> 36684, we accept -5.  Sites 2 and 3 (attribute-declarations and
  leading attributes 117, statement attributes 48) to 8.6.
- Decision (2026-11-23, from the user: low-hanging fruit, 2-3k): after 7.5,
  four cheap batches by the work list's extension groups (4691 declarations
  in all): (8.1) clang's type traits, ~770 (__has_unique_object_representations
  312, __is_layout_compatible 113, __is_trivially_relocatable 71,
  __is_trivially_equality_comparable 69, __is_literal(_type) 95, ...);
  (8.2) attributes clang takes with little effect, ~600
  (require_constant_initialization 133, target_version 98,
  reentrant_capability 91, address_space 81, code_seg, consumable,
  preferred_type, unavailable, init_priority, cpu_specific); (8.3) builtins,
  ~300 (__builtin_stdc_* bit operations, __builtin_is_within_lifetime,
  __builtin_infer_alloc_token); (8.4) split [dcl.attr]'s 456.  enable_if
  (185, overload resolution) after them.
- Decision: [jdahlin/vroom#550] adds the same cxx-run cache as step 2 and one
  vpp run per test in cxx-sema.  When it lands, merge origin/main at once,
  take its cxx-run.py (carry the texts/files digest split if it lacks it)
  and make --why read the unsupported output from its single run.
- Queue: a specialization's base-clause not analysed (we accept +10, part.spec.cpp); default initialization of an instantiated static member without an initializer (the +6 of 2.2); the +11 of 1.3b and +2 of 1.1/1.2 (not yet named); vpp segfaults on two clang test files (found in the -std=gnu++20
  sweep); `semantic_kind_name` lacks complex_value, complex_part,
  statement_expression (wrong names in tree dumps).
- 8.6a: attribute-declarations (`[[]];`, `[[unknown]];`) and attributes clang
  does not know (`__has_cpp_attribute` 0) are ignored ([dcl.pre]/4,
  [dcl.attr.grammar]/6); `[[fallthrough]]`/`[[likely]]` before a declaration
  is ill-formed (clang's error); the refusal left at simple-declaration.cpp
  has a detail.  Unsupported 36684 -> 36654, we reject 492, we accept 5423
  unchanged.
- 8.6b: attributes before a statement that change nothing or that clang
  does not know are ignored, also before a declaration statement
  ([stmt.pre]/1, [dcl.attr.grammar]/6).  Unsupported 36654 -> 36652, we
  reject 492, we accept 5423 unchanged.  Left: [[assume(e)]]
  ([dcl.attr.assume]), [[clang::musttail]].
- 8.6c: the attributes before a label that change nothing or that clang does
  not know are ignored ([stmt.label]/1).  Unsupported 36652 -> 36649, we
  reject 492, we accept 5423 unchanged.  Left: namespace-definition.cpp
  :327/:337/:525 (address-space and annotate_type attribute placements), the
  musttail/assume statements.
- 8.6 (5e97ae383, 3174d0e14, ae7f39b45, 610c28655): attribute-declarations,
  unknown attributes ignored, leading/statement/label attributes that change
  nothing.  Unsupported 36684 -> 36649.  Left: [[assume]] (its argument is
  not parsed as an expression), [[clang::musttail]].
- 8.7 (feb091973, 2e5c501c5; refactor, no metric moved): the stdc builtins'
  registrations generated from the pinned Builtins.td by
  gen-builtin-registrations.py (frontend/stdc-builtins.inc) for both front
  ends; 0a8827707's hand-written C++ rows removed.
- Decisions (2026-11-23, from the user): token use: briefs point at the
  plan's "Agent rules and gates", the step log lives here, reports are 15
  lines; larger chunks: an agent takes 3-5 related causes.  Builtins and
  type traits left: ~1150 of 36684 (traits 725, unknown builtins 229, other
  builtin refusals 201); next 8.8 traits, 8.9 builtins.
- 8.8 (83436e2e6, e34dfd9b3, 75f0dd98e, 5a862a262, f782c1ecf): unique object
  representations of bit-fields and no_unique_address, __is_lvalue_expr /
  __is_rvalue_expr, standard layout with bit-fields and bases, aggregates
  with bases, __is_nothrow_convertible of scalars.  Unsupported 36649 ->
  36517 (-132).  A full count of all refusals (not only first ones) added
  traits behind other causes: __has_trivial_* / __has_nothrow_*,
  __reference_*_temporary, __is_pointer_interconvertible_base_of,
  __array_extent; builtins __builtin_align_up/down/is_aligned,
  __builtin_get_vtable_pointer, __builtin_invoke (8.9).
- 8.9 (e63007fbe, 4d81d1a6a, 95068b37c, f485ab10e, 9f000770a): GNU
  __has_trivial_* / __has_nothrow_* for scalars and trivial classes, the
  reference-from-temporary traits, __is_pointer_interconvertible_base_of,
  __builtin_align_up/down/is_aligned (analysis), __array_extent.
  Unsupported 36517 -> 36494, agree +223 (later refusals).  Review: the
  align builtins were added as a hand-written row although they are
  Builtins.td records the generator can read (as 8.7's stdc family): 8.10
  moves them.  Gaps: int&& r(A{}) refused in reference_binding; direct
  initialization of a reference ignores explicit conversion functions;
  pre-C++17 prvalue copy/move not modelled.
- 8.10 (6cb97b9c3, 56f6873c2, 564dec6cd and the next): the C++-only
  CustomTypeChecking builtins (align_up/down/is_aligned, get_vtable_pointer,
  invoke, is_within_lifetime) are rows generated from Builtins.td's records
  (cxx-builtins.inc, gen-builtin-registrations.py); the align builtins fold
  for integers through the fold table (power of two up to 2^(width-1), modulo
  the operand's type) and lower for a constant alignment; get_vtable_pointer
  is analysed and lowered (the object's first word).  Unsupported 36494 ->
  36493, we reject 492, we accept 5423 unchanged.  Left: align_* of pointers
  (constant pointer values keep no alignment), __builtin_invoke with a
  reference_wrapper, __builtin_is_within_lifetime (138 declarations; a
  constant pointer value keeps no union member, and no lifetime of other
  objects: needs the evaluator's pointer to carry the designated subobject's
  type), now refused with that reason.
- 9.1 (dropped once): [class.copy.ctor]/7, the implicit copy constructor
  X(X&) when a subobject's copy takes a non-const reference: -141, but we
  reject +38 from __is_trivially_constructible choosing A(A&) over B's
  implicit B(const B&) (cwg6xx.cpp:1160, class.copy/p12-0x.cpp:76).
- Decision (rule change): a small bug a fix exposes is fixed first by the
  same agent, as its own commit (the drops kept costing a whole agent).
- 9.2 (c8b42030e, e9aac844f, cfda5040c): __is_trivially_constructible of a
  class from its own type chooses the copy/move constructor by overload
  resolution (trivial_copy_selected: source constness and value category,
  user-declared X(X&) beside X(const X&) = default, as CWG 683); the small
  exposed bug: X(const X&) = default beside a user X(X&) was no candidate
  (declared as the implicit one now).  [class.copy.ctor]/7: the implicit copy
  constructor is X(X&) when a base or member has no const copy constructor
  (has_const_copy_constructor, declare_implicit's nonconst_copy);
  X(const X&) = default where that applies is refused with a reason (a
  we-accept otherwise: ill-formed before C++20, deleted from C++20).
  Unsupported 36493 -> 36382 (-111), we reject 492 and we accept 5423
  unchanged.  Details for the queue: defaulted comparison operators 180
  ([class.compare.default], constructor.cpp:874), aggregate prvalue
  temporaries (initializer.cpp 233, member-access.cpp 227,
  conversion-function.cpp 203), default member initializers in unnamed
  classes 217 (an unnamed struct's or anonymous union's implicit constructor
  has no mangled symbol: removing the refusal passes the analysis and fails
  the lowering, "symbol is not mangled yet"), a deleted subobject destructor
  197 (virtual-function.cpp:328: the vtable slot of a deleted destructor).
  Left: the implicit copy assignment's X& operator=(X&) form
  (implicit-assignment.cpp subobject_assignment returns unknown for it).
- 9.3 (746a6ecf8, 4e4f4a73d, e3a9e4933): aggregate prvalue temporaries: the
  block-scope lowering (temporary_object, compound_literal) already handled
  them, so the refusals went for a member of one with a destructor or of a
  base, a conversion function called on one (non-template, accessible result
  destructor) and a reference to its base class; kept: namespace scope
  (static temporary), arrays, conversion function templates (ordering of
  Bar<X, enable_if<..>> vs Bar<Foo<T>>, GH18291) and a temporary's
  inaccessible destructor (no analysis check exists for any temporary, p4.cpp:509).
  Unnamed class default member initializers: an anonymous union's initializers
  run in the enclosing constructor (anonymous_member_initializers; clang has no
  constructor for it), an unnamed class of a class is mangled Ut<n>_; exposed
  and fixed: nested anonymous unions' multiple initializers (variant_initialized),
  const-default-constructible /7.6, a non-const lvalue reference bound to a
  conversion function's lvalue (we reject 492 -> 479).  Pre-C++20 constexpr
  defaulted constructors that leave an anonymous union uninitialized are still
  refused ("dcl.constexpr", the rule is not implemented).  The implicit copy
  assignment X& operator=(X&) is declared; no metric moved.  Unsupported
  36382 -> 36214.
- Review of 9.3 (orchestrator): 746a6ecf8 was blessed with we reject 495
  (+3, p5-var.cpp:95), fixed by the next commit 4e4f4a73d; the tip is
  right, that one commit alone is not (left as is: pushed history).
  fe-bench per step +0.49 %: watched against an older base.
- 9.4 (ec800bf08, 9ddf9d15b): a class prvalue bound to a namespace-scope
  reference has static storage (_ZGR<name>_, destroyed at exit); a refused
  static_assert or constexpr initializer says why.  Unsupported 36214 ->
  36131.  Dropped once: C++11's implicitly const constexpr member function
  (-189 unsupported, we reject +9; scratchpad cxx11-constexpr-const.patch).
- 9.5 (0d20ac46c, ffe9aac47, 6cd4ecc3f, f39b66655): `x = {...}` of a class
  whose operator= is not its copy assignment is refused (was: temporary of the
  class); value-initialization keeps its zeros for an implicit default
  constructor's base and member constructions (evaluator `zeroed`); a
  discarded class prvalue in a comma is the evaluator's; before C++20 a call
  of a virtual function is not constant.  Unsupported 36131 -> 36126, we
  reject 479 -> 475, we accept 5423 -> 5419.  C++11's implicitly const
  constexpr member function still not applied: scratchpad
  cxx11-constexpr-const-v3.patch (also out-of-class definitions, member
  templates, `&S::tm` deduction against a const target) gives unsupported
  -186, we reject +3, we accept +1; what blocks it: SemaCXX/cxx0x-initializer-
  stdinitializerlist.cpp:237 (f({{1,2},{3,4}}) for initializer_list<int[2]>
  vs <int[2][2]> is ambiguous for us, a C++14 bug too), its PR12436::X layout
  (dsize 1, clang 0), and five we-accept lines (p2-0x.cpp:315, constant-
  expression-cxx11.cpp:1333 and :2539 need "constexpr function never produces
  a constant expression" for B().x with a non-literal B; stdinitializerlist
  :176 and :262 backing-array / deleted-constructor diagnostics).
- 9.6 (5de58662a, f7444722c): initializer_list<array> conversion sequences
  ([over.ics.list]/5, /8), list-init to initializer_list ranked first
  ([over.ics.rank]/3.1.1), {} with an initializer-list constructor, a
  constructor template makes a class non-POD for layout (Itanium; layout
  refuted 365 -> 219), C++11 implicitly const constexpr member functions,
  constexpr default-initialized objects of classes with a constructor.
  Unsupported 36126 -> 35877, we reject 475 -> 456, we accept +12 (named in
  the commits: constexpr evaluations not constant that clang diagnoses).
  Review: 5de58662a holds four fixes (the formatter failed the first commit
  and the second took everything); left as is.  Left: the deleted
  destructor's vtable slot (__cxa_deleted_virtual, 197), the evaluator's
  value of a default-initialized constexpr object.
- 9.7 (05474eab9, ba2af6b34): a constexpr object its default constructor
  initializes is usable in constant expressions; a conversion function on
  an aggregate prvalue in a namespace-scope static_assert.  Unsupported
  35877 -> 35862.  Findings: "kept no value" is mostly an aggregate's default
  member initializer naming another member refused (initializer.cpp:2085);
  namespace-scope dynamic initialization has no temporaries in lowering
  (170: design needed); the deleted destructor's vtable slot works
  (scratchpad deleted-virtual-slot.patch) but needs [class.dtor]/6's error
  for a constructor or variable that uses it.
- 9.8 (a55934ef8): a deleted destructor: __cxa_deleted_virtual slots, and the
  errors for a variable, a defaulted default constructor and a user-provided
  constructor of such a class.  Unsupported 35862 -> 35855, we accept 5431
  -> 5419 (12 declarations clang rejects now rejected), types/constants
  verified -6 (claims of declarations now rejected).  Queue: typeinfo
  (_ZTI/_ZTS) emitted with a vtable (the patch's test avoided it).  Needs a
  design: an omitted aggregate element's default member initializer that
  uses `this` (analysis, evaluator's partial object, lowering).  Next (9.9):
  the evaluator records why it answers not_constant; the 365 of
  simple-declaration.cpp:701 split by that reason.
- 9.9 (2 commits): the evaluator records the line of its last not_constant
  answer (155 of the 365: reading an inactive union member, L1283); a
  variant member's default member initializer only when no other variant
  member is initialized ([class.base.init]/9).  Unsupported +1, we reject
  -1, we accept -6.  Decision: a correctness fix may raise unsupported a
  little (a newly analysed declaration reaching a later cause); refuted and
  we reject still must not rise.  Next (9.10): three union evaluator bugs
  (a static member through an inactive member, anonymous member nesting, an
  implicit constructor's default member initializer), then the rejection
  (scratchpad rule-and-ctor.patch).
