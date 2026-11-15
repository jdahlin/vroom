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
