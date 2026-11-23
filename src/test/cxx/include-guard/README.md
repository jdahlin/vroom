# The multiple-include guard skip

Cases for the C++ preprocessor's multiple-include guard
(`src/frontend/cplusplus/preprocessor/include-directive.cpp`, clang's
MultipleIncludeOpt): each directory is one translation unit, `main.cpp` and
the headers it includes.  `bench/cxx-include-guard.py` preprocesses each with
vpp (`-fcxx-dump=preprocessed`) and with `clang -E -P`, and compares the text
and the diagnostics (`-Wall -Wextra -Wunused-macros`).  A `LINKS` file lists
hard links to make before the run (`NEW EXISTING` per line): git keeps none.
