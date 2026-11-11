# What C beginners actually misunderstand, and what a compiler can do

Research revision, 2026-11-11. Scope: a **general C compiler**, without a new language,
mandatory project manager, or platform-specific onboarding workflow. The question is
which real learner misunderstandings diagnostics can help repair. The earlier
[modern-language comparison](compiler-onboarding-modern-comparison.md) is background,
not the basis for prioritizing C problems. No compiler implementation changes are made.

## Findings

Prioritize explanations of **values and addresses; arrays and length; strings and
storage; initialization and lifetime; and declarations versus linked definitions**.
These connect existing compiler evidence to questions learners actually ask. Add
carefully controlled teaching for integer division and assignment in conditions,
where legal C can conflict with a learner's intention. Input parsing also needs
runtime examples: compiler prose cannot establish what a human will type.

This is an intervention priority, **not a measured ranking of C error frequency**.
The research below combines course studies, independently recurring public questions,
and small compiler probes. Search counts, views and votes are not prevalence data.
A syntax slip, missing strategy, and incorrect mental model require different help.

## Method and limits

Conducted a purposive search on 2026-11-11 for C novice studies, pointer/array errors,
`scanf` address arguments, null termination, initialization, integer division and
undefined references. Inspected the linked question statements to identify what
learners believed; answers were checked against actual compiler behavior and teaching
sources rather than treated as authoritative C semantics. Included C questions and
C-focused communities; excluded C++-only examples from the evidence clusters. Multiple
answers and duplicate URLs for one question count as one example.

This is not a systematic review, random sample, or analysis of all question traffic.
Searches were concept-directed and therefore cannot prove the concepts dominate
learner problems. Public questions overrepresent people who get stuck and post in
English; course tasks overrepresent what the course teaches. Some older examples use
obsolete defaults or poor advice. Demographics and prior experience are generally
unknown. An observation of wrong code alone does not prove its author's mental model.
Below, distinguish an explicit question from an inferred explanation.

For concrete compiler evidence, compiled 15 independent minimal snippets on x86-64
Linux using GCC 15.2.0 and Clang 23.1.6, at both `-O0` and `-O2`, with:

```text
-std=c17 -Wall -Wextra -Wconversion -pedantic-errors -fdiagnostics-color=never -c
```

The [probe evidence](compiler-onboarding-probes.json) records exact versions, source,
commands, exit status and complete diagnostics: 60 short compilations, no test-suite
build. These flags are an experimental baseline, not a recommended beginner preset.
Results can depend on target, optimization, libc header attributes and compiler
release. No sanitizer or analyzer execution was performed. Successful compilation
with a warning is distinct from rejecting the program; unrelated warnings do not
count as detecting the misunderstanding under investigation.

## Research evidence beyond anecdotes

- **Albrecht and Grabowski, C course:** manually inspected 12,371 submissions by 280
  students, including logic errors rather than compiler failures alone. Their abstract
  reports substantial slips and difficulties with strategic problem solving. This
  supports improving the repair process, but cautions against treating every failed
  program as a missing diagnostic. The abstract does not provide a frequency ranking
  for our eight clusters. [Author's study page](https://www.swe.informatik.uni-goettingen.de/publications/sometimes-its-just-sloppiness-studying-students-programming-errors-and-misconceptions).
- **Bosse et al., C/Python CS1 catalog:** analyzed work by 166 engineering students and
  catalogued 41 antipatterns from 95 misconceptions across the two languages. Useful
  evidence that programming mistakes have contextual explanations, not a global C
  frequency estimate or proof all proposed diagnostics help. [Technical report and methodology](https://arxiv.org/html/2104.12542v1).
- **Fu et al., C error logs:** studies syntax-error types and resolutions from an
  undergraduate course. Supports treating locating/recovering from syntax slips as a
  real task; the inspected abstract alone cannot support a numerical ranking.
  [ICCE 2015 paper record](https://library.apsce.net/index.php/ICCE/article/view/3276).
- **Park, tools/settings for introductory C:** surveys environment setup, warning
  settings and analyzers, with recommendations revised in an operating-systems
  course. Warns that aggressive warnings can confuse students. This is practitioner
  guidance, not a controlled first-programming-course trial.
  [ASEE 2024 paper](https://nemo.asee.org/public/conferences/344/papers/41833/view).
- **Pettit et al., enhanced errors:** reports no measured benefit on its repeated-error
  outcome despite positive impressions. The inspected abstract is not C-specific;
  use it as a methodological warning, not an estimate of effects for C learners.
  [Study record](https://digitalcommons.acu.edu/info_tech_computing/6/).

Harvard's C teaching materials explicitly connect addresses, pointer operations,
strings, allocation and memory errors. This is pedagogical corroboration that these
concepts belong together, not evidence of their relative frequency.
[CS50 memory lesson](https://cs50.harvard.edu/x/notes/4/).

## Eight issue clusters and responsible interventions

All example diagnostics labelled **proposed** are design sketches, not quotations
from GCC/Clang. Probe names refer to the machine-readable evidence file.

### 1. A function receives a value; writing elsewhere needs an address

**Learner evidence.** One question asks why `scanf` needs `&` and whether its absence
fails at compile time or runtime. Another asks whether `&array` becomes a double
pointer and describes an array as storing a pointer. These reveal several linked
misunderstandings, not merely a forgotten character.
[Address question](https://stackoverflow.com/questions/2062648/how-does-the-scanf-function-work-in-c),
[array/address question](https://stackoverflow.com/questions/56951091/use-of-ampersand-operator-in-scanf).

```c
#include <stdio.h>
void f(void) { int n = 0; scanf("%d", n); }
```

**Observed:** both compilers warn about the format/argument mismatch, return success,
and identify the `int *` versus `int` contract (`address`). Proposed additional help:

```text
note: '%d' asks scanf to store an int through an address
help: if the input should replace n, pass &n
```

Explain that C still passes the pointer **by value**. `&` is an operator, not a special
`scanf` annotation. No automatic blanket insertion: an existing `int *p` needs `p`,
not `&p`, and `char a[8]` differs from `&a`. Check return status as a separate input
lesson. A bad conversion is a library contract violation, even if the executable
happens to seem to work. Test ordinary pointer parameters as well as format functions.

### 2. An array, its first-element pointer, its storage size and its element count differ

**Learner evidence.** Distinct questions ask how to retrieve an array's size through a
pointer, why `sizeof` changes in a function, and where length information is stored.
[Pointer length](https://stackoverflow.com/questions/492384/how-to-find-the-size-of-an-array-from-a-pointer-pointing-to-the-first-element-a),
[parameter adjustment](https://stackoverflow.com/questions/28187738/sizeof-applied-to-array-types),
[where size lives](https://stackoverflow.com/questions/4524862/how-does-an-array-pointer-store-its-size).

```c
#include <stddef.h>
size_t count(int a[10]) { return sizeof a / sizeof a[0]; }
```

**Observed:** both compilers warn (`array_size`). Proposed help should explain two
specific transformations: the parameter declaration adjusts to a pointer type, and
the caller's array expression typically converts to a first-element pointer. Neither
passes a general element-count value. Suggest a separate `size_t count` parameter,
not a fabricated constant or a rewrite to `sizeof *a`.

For `int a[3]; a[3]`, Clang warns at both optimization levels, GCC only at `-O2` in
our initialized probe (`array_offbyone`). Label the valid index range 0–2. Preserve
legal one-past pointers without dereference and `sizeof` on an actual local array;
a warning about pointer-size division should not ban deliberate size calculations.
General runtime indices require range analysis or optional instrumentation.

### 3. A character buffer is not automatically a managed string

**Learner evidence.** A question explicitly asks what termination means, whether it
is `NULL`, and whether the compiler adds it. Another combines one `char`, `%s`, and
changes to `&` until execution appears to work. [Termination question](https://stackoverflow.com/questions/72436704/what-are-null-terminated-strings),
[character/string confusion](https://stackoverflow.com/questions/50074855/the-usage-of-an-ampersand-in-printf-and-format-specifiers).

```c
#include <stdio.h>
void f(void) { char a[3] = "cat"; puts(a); }
```

**Observed:** both current compilers warn that the initializer omits termination
(`string_termination`). This initializer is legal C; using the resulting buffer as
an unbounded C string is the problem. Proposed help: show three character slots and
explain the fourth byte needed for `\0`; offer `char a[] = "cat"` when a string is
intended. Do not suggest annotating it as nonstring as the beginner's first remedy.
Raw byte arrays are legitimate and must remain possible.

`a == "yes"` warns in both compilers; `a == b` for two `char *` parameters is silent
(`string_compare`, `string_identity`). The latter may deliberately compare identity.
An explanation can contrast address equality with `strcmp(a,b) == 0`, but must not
auto-rewrite all pointer comparisons or assume either pointer denotes a valid string.
For output, relate `%s` to string storage and `%c` to a character; generic “wrong type”
help can miss the underlying representation problem.

### 4. Input parsing is stateful and its format is a contract

The `scanf` questions above show type-level confusion; input also depends on what
was consumed and whether conversion succeeded. Separate these lessons instead of
teaching “add `&` and it works.” This cluster's additional newline/bounds scenarios
are design candidates, **not independently established prevalence claims**.

```c
char name[8];
scanf("%s", name);           /* no maximum token length */
scanf("%d", &number);
scanf("%c", &letter);        /* may consume the following newline */
```

**Observed:** Clang is silent; GCC at `-O2` warns only about ignored return values,
not about the missing width or whitespace behavior (`input_bound`, `input_newline`).
Both are silent at `-O0`. A directly known buffer can support a controlled warning
about unbounded `%s`, with `%7s` as an intent-dependent suggestion. Width counts must
reserve termination space. Do not blindly apply that to `%c`, wide input or allocated
extensions, and do not claim `%7s` reads arbitrary full names containing spaces.

A whitespace-before-`%c` suggestion changes what input is accepted and can block
waiting for a non-whitespace character. Teach it with a concrete input trace, not an
unconditional source repair. `fgets` plus deliberate parsing can be a documented
alternative. Runtime tests should exercise long input, malformed numbers, EOF and
whitespace; compiler-only checks cannot prove these cases work.

### 5. Declaration, initialization, scope, and lifetime are separate facts

**Learner evidence.** A question asks for a Perl-like test of whether a local C
variable has a value. Another expects a declared counter to be usable and mixes it
with input handling. [Initialization-state question](https://stackoverflow.com/questions/36490948/how-to-check-if-a-variable-has-been-initialized-in-c),
[counter example](https://stackoverflow.com/questions/46165304/how-to-solve-uninitialized-local-variable-used).

```c
int f(int ready) { int n; if (ready) n = 1; return n; }
```

**Observed:** Clang warns with the false path and use; GCC is silent at both levels
in this probe (`uninitialized`). Proposed help should name that path and require a
meaningful assignment or return on it. Auto-inserting zero can hide an algorithmic
error. Do not describe uninitialized reads as a reliable random-value generator.
Static storage initialization has different rules and needs a positive neighbor.

A separate `scope` probe referring to a name after its block is rejected by both.
Help should distinguish “not visible here” from “visible but never initialized.”
Suggesting an outer variable declaration can change semantics; never infer that
moving it is always correct.

### 6. The pointer's lifetime differs from the allocated object's lifetime

**Learner evidence.** A question fears returning a locally declared pointer even
though the pointed-to memory was allocated. Another asks what survives a function
return. [Local pointer concern](https://stackoverflow.com/questions/16532983/is-returning-a-local-pointer-cause-undefined-behaviour),
[return/lifetime question](https://stackoverflow.com/questions/26009989/how-memory-space-behave-after-function-return-for-local-variables).

```c
int *bad(void) { int n = 3; return &n; }
/* A local pointer holding malloc's result can instead be returned validly;
   the allocated object's lifetime does not end with the pointer variable. */
```

**Observed:** both compilers warn on `bad` (`lifetime`). A minimal checked allocation,
free, then dereference warns in GCC and is silent in Clang's ordinary compile
(`use_after_free`). This says nothing about their separate analyzers.

Proposed diagnostics should label creation, lifetime-ending event and later use when
provable. Distinguish `sizeof p` from `sizeof *p` for allocation mistakes, and explain
that assigning a pointer does not copy the allocation. Do not “fix” every returned
local address with `malloc`: it introduces failure handling and an ownership/free
contract. Valid caller-owned output, static storage and returned values are different
designs. Optional analyzer/sanitizer modes can demonstrate more dynamic errors, with
explicit limitations; ordinary successful compilation is not a memory-safety proof.

### 7. The surrounding destination does not determine an expression's arithmetic

**Learner evidence.** A C learner expects a float destination to preserve a fractional
result from integer operands; another C-focused help thread independently encounters
integer division. [Explicit destination-type expectation](https://www.reddit.com/r/C_Programming/comments/1bvdfzl/),
[CS50 arithmetic repair](https://www.reddit.com/r/cs50/comments/16hsnhh/).

```c
double average(int total, int count) { return total / count; }
```

**Observed:** silent in both compilers at both levels (`division`). Legal and sometimes
intentional. A controlled pedagogical warning could explain that integer division
occurs before conversion to double, showing `(double)total / count` **if** fractional
results are intended. Casting the completed quotient is not the same repair. Handle
zero divisors separately; do not auto-convert integer arithmetic throughout a program.
Signed/unsigned conversions deserve targeted explanations of concrete ranges/types,
not claims that every conversion is erroneous.

Assignment/equality is another legal-expression trap with independent questions:
[what `==` means](https://stackoverflow.com/questions/2154691/what-is-the-meaning-of-in-c),
[why assignment in `if` is accepted](https://stackoverflow.com/questions/62100980/can-assignment-operator-be-used-in-if-statements-as-if-statements-are-used-bool).
Both compilers warn in our `assignment` probe. Explain changing the value versus
testing equality; offer comparison and deliberate parenthesized assignment as
alternatives. Never auto-select intent or prohibit idiomatic input loops.

### 8. A header declaration is not a linked implementation; an editor is not a toolchain

**Learner evidence.** A self-described first-time programmer cannot interpret an
undefined reference to a teaching-library function despite including its header.
Another expects including `<math.h>` to resolve `sqrt`.
[First-programmer library failure](https://stackoverflow.com/questions/71507578/undefined-reference-to-get-string-compiling-cs50-code),
[math header versus linking](https://stackoverflow.com/questions/5248919/undefined-reference-to-sqrt-or-other-mathematical-functions).

```c
int answer(void);
int main(void) { return answer() != 42; }
/* Compiling an object can succeed; final linking needs a definition. */
```

This cluster is supported by question evidence; no link/setup experiment was run in
the 60 frontend/code-generation probes. A driver/linker should identify the stage,
symbol and referencing source when known; explain declaration versus definition;
and suggest checking supplied objects/libraries. It cannot know every library's
package name or infer a missing source file from a symbol. Normal unresolved object
references under `-c` are not errors. Library ordering and platform differences require
specific context, rather than an unconditional `-lm` recommendation everywhere.

Missing standard headers, a missing compiler executable and a shell failing to run
an output are different setup failures. A generic compiler should report the facts
available to its own stage and offer searchable next steps. A tutorial can teach how
to inspect the actual build command without turning every compiler error into a
platform installation guide.

## Beginner populations: design hypotheses to test

A first programmer may need the concepts “value,” “address,” “function argument,” and
“build step” introduced concretely. Someone coming from Python/Java may know control
flow but expect managed string objects, retained collection length or different
initialization rules. The Perl question above explicitly demonstrates one form of
cross-language transfer; it does not prove that all experienced-language learners
share it. Do not assign a population or infer educational background from code.
Offer a compact fact first, optional concept explanation second, and deeper examples
on demand. Test both populations rather than make diagnostic verbosity a proxy for
experience.

## First implementation slice and acceptance tests

Prioritize **address/format, array-parameter count, termination, path initialization,
and lifetime** first: each has concrete evidence a compiler can connect to a source
cause. Improve the meaning of existing diagnostics before adding warning categories.
Add build-stage help in the driver/linker separately. Treat arithmetic teaching and
stateful-input guidance as opt-in/explanatory until false-positive costs are measured.

For each family, acceptance requires:

| Family | Positive diagnostic/repair test | Required negative/ambiguity neighbor |
|---|---|---|
| Value/address | `%d` with `int` labels argument and format, explains storage via address; justified repaired example checks conversion result | Already-correct `int *`, array input, incompatible pointer; no blanket `&` edit |
| Array/count | `sizeof` count in array parameter labels adjusted type and declaration; separate count example works | Actual array `sizeof`, deliberate pointer-size query, legal one-past pointer |
| String storage | Known unterminated array passed to string consumer shows storage requirement | Valid raw nonstring bytes, explicit length output, identity comparison, unknown termination |
| Initialization | Concrete path and use are linked; both repaired branches produce intended results | Static zero initialization, assignment before every use, genuinely unknown flow; no unconditional zero fix |
| Lifetime | Returned local address and provable post-free use show cause/use | Returned allocated pointer, caller-owned output, legitimate value return; no automatic allocation |
| Build stage | `-c` succeeds with external reference; link without definition fails clearly, link with definition succeeds | Header alone, missing library, similarly named unrelated symbol; no speculative package command |
| Arithmetic/input teaching | Integer quotient vs fractional quotient and token/newline behavior demonstrated with actual values/input | Intended integer division, deliberate whitespace capture, EOF; no intent-changing automatic edits |

Preserve C semantics, standards selection, ABI, warning controls, valid low-level
idioms and existing build systems. Each new warning needs a documented rationale and
suppression path. Related spans and short help are useful only when they explain the
specific evidence; a generic paragraph about pointers on every error is noise.
[GCC warning controls](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html),
[Clang diagnostic groups](https://clang.llvm.org/docs/DiagnosticsReference.html).

Technical validation includes snapshots, exact edit applicability, fixed-example
compilation, independent-error recovery, macro/source attribution, narrated/plain
output and unchanged successful-build performance. A stale diagnostic must not
rewrite newly edited source. Runtime/analyzer examples must say which checks ran and
what they cannot establish.

Pedagogical validation must measure **correct repairs and understanding**, not just
whether a message sounds friendly. In a small formative trial, record time to first
justified repair, repeated same-error submissions, wrong edits introduced, use of
optional explanation and ability to explain a nearby variant. Compare concise baseline
with the proposed help, include both learner populations, and report task-specific
limits. Five to eight users can expose design failures but cannot establish universal
effect sizes or prevalence. Larger claims need an appropriate study.

## Stable 25-case starting corpus

IDs B01–B25 identify an **initial engineering/teaching corpus**, not the 25 most
frequent mistakes in a measured population. Cluster evidence supports the themes;
individual rows without a linked learner example are hypotheses for validation.
The safe neighbors below establish distinctions a compiler must preserve; they are
not universally valid automatic repairs. Fragments assume suitable declarations,
headers and enclosing functions. Runtime repairs still require meaningful tests.

| ID | Trigger/example | Safe neighbor or justified alternative | Caveat/evidence |
|---|---|---|---|
| B01 | `int n = 3` then next statement | `int n = 3;` | Syntax slip; `semicolon` probe and C error-log study. Recovery must not blame the next statement. |
| B02 | `if (ready { use(); }` | `if (ready) { use(); }` | Syntax/recovery candidate; test unmatched `()`, `[]`, `{}` separately and note opening location. |
| B03 | `int count=3; use(coutn);` | `use(count);` if intended | Typo candidate; equally plausible visible names forbid automatic selection. |
| B04 | Calling `puts` without declaration | Include `<stdio.h>` before call | Declaration concept; also test a custom prototype and missing installed header. Do not invent declarations. |
| B05 | Call declared `answer` but omit its defining object from link | Link the actual definition; retain valid `-c` | Cluster8; header/library questions establish the confusion. |
| B06 | `if (n = 3)` where comparison intended | `if (n == 3)`; intentional `if ((n = next()))` | Cluster7 and `assignment` probe; preserve intentional assignment. |
| B07 | `if (ready); use();` | `if (ready) { use(); }` when conditional use intended | Empty-body candidate; empty loops can be deliberate. No unconditional semicolon deletion. |
| B08 | `if (ready) first();` then indented `second();` | Braces around both, if both intended conditional | Indentation candidate; adding braces changes control flow, so propose alternatives. |
| B09 | `if (0 < x < 10)` | `if (0 < x && x < 10)` if range check intended | Legal chained-comparison candidate; some bool/integer comparisons are deliberate. |
| B10 | `double q = a / b;` with integer operands | `(double)a / b` if fractional quotient intended | Cluster7; silent in probes. Preserve intended integer truncation and check divisor separately. |
| B11 | `int n; if (ready) n=1; return n;` | Assign or return meaningfully on both paths | Cluster5; no universal zero initializer. |
| B12 | `int f(int n) { if (n) return 1; }` | Return an appropriate value on the other path | Control-flow candidate; distinguish special `main` fallthrough and functions that cannot return. |
| B13 | `scanf("%d", n)` | `scanf("%d", &n)` with result checked | Cluster1; already-correct pointer arguments must not acquire `&`. |
| B14 | `printf("%d", long_value)`; `scanf("%f", &double_value)` | `%ld` for `long`; `%lf` for `double *` input | Cluster1/3; printf float promotion differs from scanf storage type. Match target typedefs. |
| B15 | `scanf("%d", &n); use(n);` | `if (scanf("%d", &n)==1) use(n); else handle_input();` | Runtime/input candidate; do not assume conversion succeeds or zero-initialize to conceal failure. |
| B16 | `char a[8]; scanf("%s", a);` | `%7s` with checked result, if token input intended | Cluster4; width and termination space; raw `%c`, wide input and full-line input differ. |
| B17 | `%d` input then `%c` unexpectedly reads newline | Deliberate whitespace policy or line input/parser | Cluster4; actual input trace required. Leading space changes behavior; no unconditional fix. |
| B18 | `sizeof a/sizeof a[0]` for parameter `int a[]` | Pass count separately; use expression on an actual array | Cluster2; preserve intentional pointer-size queries. |
| B19 | `int a[3]={0}; use(a[3]);` | Last valid element `a[2]`, if intended | Cluster2; preserve legal one-past pointer formation and loops with explicit counts. |
| B20 | `a == "yes"` intended as textual comparison | `strcmp(a,"yes")==0` for valid strings | Cluster3; preserve identity comparisons, null checks, byte buffers and unknown encodings. |
| B21 | `char a[3]="cat"; puts(a);` | `char a[]="cat";` when string intended | Cluster3; raw unterminated arrays are legal. Diagnose use/context without changing C rules. |
| B22 | `int *p; *p = 1;` | `int n; int *p=&n; *p=1;` for local object | Pointer/storage hypothesis; declaring a pointer allocates no pointee. Do not always introduce malloc. |
| B23 | `struct S *p=malloc(sizeof p);` | `malloc(sizeof *p)` when allocating one `struct S` | Cluster6 design candidate; preserve intentional pointer-sized allocations and check failure. |
| B24 | `int *f(void) { int n=3; return &n; }` | Return a value or use documented caller-owned storage | Cluster6; returning a local pointer to separately allocated memory is a safe distinct case. |
| B25 | `free(p); use(*p);` | Complete use before free, if ownership permits | Cluster6; pointer assignment/NULL alone does not repair aliases or ownership. |

For each ID, instantiate at least one complete failing/suspicious program, one
justified repaired program, and one legal near-neighbor before claiming support.
Record standard, target, flags, source positions, notes, applicability, exit status
and runtime input/output where relevant. The 15 minimal probes are a baseline for
part of this corpus; they do **not** establish implementation or acceptance of all
25 entries. B02/B03/B04/B07/B08/B09/B12/B14/B15/B22/B23 and complete link/setup cases
still require their dedicated baseline experiments and learner evidence before
stronger coverage claims.
