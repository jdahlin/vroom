# Clang bugs found

Crashes of the reference compiler met while testing vcc against it.  None
is reported upstream yet.  Add one here with the smallest input that shows
it, the version, and how it affects a harness.

## 1. SemaCXX/using-decl-templates.cpp: segmentation fault

Noted in `bench/cxx-sema.py` (`check`): the harness compares only up to the
place clang crashed.  Not re-checked or reduced here.

## 2. Recursive noexcept specification: stack overflow, segmentation fault

Found 2026-11-25, clang 23.1.6 (Ubuntu, 23.1.6 ++20261116013916),
x86-64, `ulimit -s` 8192:

```c++
template <typename T> struct S {
    S() noexcept(noexcept(S<S>()));
};
S<void> t;
```

`clang++ -std=c++11 -fsyntax-only` warns "stack nearly exhausted" and dies
with SIGSEGV (3 of 3 runs) instead of reporting the instantiation depth, as
it does with a small `-ftemplate-depth`.  g++ reports the depth.

In the suite this is `SemaTemplate/instantiation-depth.cpp -std=c++11
-DNOEXCEPT` (the harness does not pass the test's `-ftemplate-depth=5`).
Where clang crashes, line 21 on is not compared; where it does not (the
premerge run of 2026-11-25 on jdahlin/vroom#594), `cxx-sema.py --check`
fails with a new "we accept" at `instantiation-depth.cpp:21`.  vcc accepts
the program, on main too: a missing recursion check of ours, which the crash
hides.

## Related: warning analysis

`bench/cxx-sema.py` also notes that clang 23's `-Wthread-safety` crashes on
SemaCXX/warn-thread-safety-analysis.cpp; the harness retries without the
`-W` options.  Not re-checked here.
