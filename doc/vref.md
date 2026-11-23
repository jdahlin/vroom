# vref

Build with `make -C src -j 6 vref`. Run `build/linux/src/vref` on Linux
or `build/src/vref` on macOS. Run from the project's compilation directory.
Pass the source file and a one-based line and **byte** column. Pass the
frontend's language, target, include, and macro options after `--`.

    vref refs example.cpp 12:9 -- -std=c++20 -Iinclude
    vref rename example.cpp 12:9 count -- -std=c++20 -Iinclude
    vref swap-param example.c 7:12 1 2 -- -DDEBUG
    vref extract example.c 8:5 10:18 update_count

`refs` prints locations including the declaration. Editing commands print
a unified diff on stdout after checking the proposed source. From a project
directory, redirect that output to a patch and review it with `git apply --stat`
or apply it with `git apply`. A refusal exits nonzero with a reason on stderr.

| Command after the file and position are supplied | Supported selection |
| --- | --- |
| `refs`, `rename … NEW` | A local variable or named function parameter selected at its declaration or use. Rename requires a fresh identifier in this file. |
| `change-type … TYPE` | One standalone scalar local declaration. |
| `delete-var` | An unused, uninitialized scalar local. |
| `move-up`, `move-down` | Exchange two adjacent, uninitialized scalar local declarations. |
| `add-param … TYPE NAME ARG` | Append a parameter and a decimal integer argument at each direct call. |
| `remove-param … N` | Remove an unused parameter and its decimal literal arguments. |
| `swap-param … N M` | Exchange two parameters and their corresponding arguments. |
| `change-param-type … N TYPE` | Change a parameter's scalar type. |
| `extract … END_LINE:END_COLUMN NAME` | Extract complete arithmetic expression statements inside a simple static function. Select from the first token through the last semicolon. The helper takes the addresses of the ordinary `int` locals and parameters used in the selection. |

Signature commands select a static free function at its definition or a call.
The first version handles one definition, named parameters, direct calls with
single-token arguments, and one translation unit. Indices start at one.
The scalar type spellings are `char`, `short`, `int`, `unsigned`, `long`,
`float`, and `double`. The tool declines unsupported declarations, ambiguous
occurrences, and edits inside macro expansions. C++ coverage follows vcc's
C++ frontend. Cross-file changes and overload-set editing are outside this version.

## Extending the tool

The query build compiles the same frontend sources with `VCC_REFACTOR`
in a separate object directory. The preprocessor supplies physical spelling
ranges and macro invocations. Each language reports declaration and use
bindings while parsing. The C++ hooks ignore tentative lookup. All positions
in the protocol are offsets into the physical main file.

Add shared operations in `src/bin/vref/vref.py`. A command plans a list of
half-open byte ranges and replacements. The shared code checks for overlap,
applies them in memory, reparses an overlay under the original file name and
compiler options, and verifies retained bindings before emitting the patch.
Extend frontend hooks when an operation needs semantic information that the
query stream does not yet contain. Run `python3 bench/vref-test.py` to check
both languages, including applying, compiling and executing the resulting edits.

## Storage migrations with recipes

`vref recipe FILE RECIPE.toml -- FLAGS` plans several edits against the same
source snapshot and validates the complete result in one frontend invocation
after the initial query. This lets a declaration, its writers and its consumers
change together. Recipe parsing uses Python 3.11 or later.

A recipe selects one function with `within`, a sequence of physical lexer
tokens ending in the opening brace. Write one token per space in match
patterns. An `@name` matches a declaration or use with the frontend binding
of that unique local or parameter in the selected scope. Other tokens match
their spelling. Each rule requests `replace`, `before`, or `after`, and an
exact match count (one by default). Use `scope = "file"` for an include or a
declaration outside the function. A replacement containing newlines inherits
the matched line's indentation. Recipes can supply complete macro invocations
as source. Edits cutting through an expansion or discarding a comment fail.

The optional `[storage]` operation describes parallel arrays by their local
names, element types, record fields, and existing output destinations. From
that table vref derives the patterns for the doubling/realloc blocks, guarded
memcpy sequence, and frees. It removes those growth and release sites and
generates a loop that copies record fields to the existing outputs. The recipe
supplies the new allocation policy, record declaration and append expressions.
Other allocation idioms need their own rules. See
[pair-columns.toml](../src/test/vref/pair-columns.toml) for a two-column example.

### Replay of the C++ parameter-list migration

Use the vref binary and [parameter-list recipe](../src/test/vref/parameter-list.toml)
from the tool checkout. Run from a separate checkout of `2df8ed5a`. On the
x86-64 Linux development host the implementation parses in its C bootstrap
dialect with these flags:

```sh
VREF_TREE=/path/to/vref-checkout
file=src/frontend/cxx/declaration/declarator.cpp
"$VREF_TREE/build/linux/src/vref" recipe "$file" \
    "$VREF_TREE/src/test/vref/parameter-list.toml" -- \
    -x c -std=gnu11 -Isrc/include --target=x86_64-linux-gnu \
    -isystem /usr/lib/llvm-23/lib/clang/23/include \
    -isystem /usr/include/x86_64-linux-gnu -isystem /usr/include > migration.patch &&
git apply migration.patch &&
test "$(git hash-object "$file")" = c73d49c71519ca57f515d558c17344709b46ecee
```

The expected blob is the reviewed parser implementation in PR #297. The
recipe runs from the original source and compiler flags alone. The regression
test and benchmark notes in that PR are separate from the storage edit.

### Extract a guarded block

An `[outline_if]` recipe keeps an `if` condition at its call site and moves
the selected compound statement into a file-local void helper immediately
before the enclosing function. Give it one `match` ending in the opening
brace, the helper `name`, and the enclosing pointer `parameters` to pass.
The helper's parameter declarations come from the original source. vref
compares the moved tokens and their recorded name bindings after reparsing,
including the bindings of arguments at the new call.

Use this standalone operation for multiline blocks in file-scope functions
whose inputs are simple named pointer parameters. It supports member access
and, in C, forwarding the pointer as an ordinary argument. Control transfers,
macro expansions and uses of additional enclosing locals are declined. The
[scope-growth recipe](../src/test/vref/scope-growth.toml) extracts the rehash
loop from `cxx_declare`. Run it with the same bootstrap flags as the storage
migration, selecting `src/frontend/cxx/names.cpp`.

For an `int` C function, `return_status = true` replaces the selected branch
with `return helper(...);`. The guard must test a single variable and the
block must end in an unconditional return of a decimal status. Parameters
may be freshly initialized local integers or pointers read by the branch.
The [vasm recipe](../src/test/vref/vasm-benchmark.toml) operates on the
prepared benchmark block in the first commit of the migration.
