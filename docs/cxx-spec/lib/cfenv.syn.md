# 26.3.1 Header `<cfenv>` synopsis [cfenv.syn]

[Contents](../README.md) · up: [26.3 The floating-point environment \[cfenv\]](cfenv.md) · prev: [26.3 The floating-point environment \[cfenv\]](cfenv.md) · next: [26.4 Complex numbers \[complex.numbers\]](complex.numbers.md)

```cpp
#define FE_ALL_EXCEPT see below
#define FE_DIVBYZERO see below    // optional
#define FE_INEXACT see below      // optional
#define FE_INVALID see below      // optional
#define FE_OVERFLOW see below     // optional
#define FE_UNDERFLOW see below    // optional

#define FE_DOWNWARD see below     // optional
#define FE_TONEAREST see below    // optional
#define FE_TOWARDZERO see below   // optional
#define FE_UPWARD see below       // optional

#define FE_DFL_ENV see below

namespace std {
  // types
  using fenv_t    = object type;
  using fexcept_t = integer type;

  // functions
  int feclearexcept(int except);
  int fegetexceptflag(fexcept_t* pflag, int except);
  int feraiseexcept(int except);
  int fesetexceptflag(const fexcept_t* pflag, int except);
  int fetestexcept(int except);

  int fegetround();
  int fesetround(int mode);

  int fegetenv(fenv_t* penv);
  int feholdexcept(fenv_t* penv);
  int fesetenv(const fenv_t* penv);
  int feupdateenv(const fenv_t* penv);
}
```

**1** The contents and meaning of the header `<cfenv>` are the same as the C standard library header `<fenv.h>`. [*Note 1*: This document does not require an implementation to support the `FENV_ACCESS` pragma; it is implementation-defined ([[cpp.pragma]](../cpp.pragma.md)) whether the pragma is supported. As a consequence, it is implementation-defined whether these functions can be used to test floating-point status flags, set floating-point control modes, or run under non-default mode settings. If the pragma is used to enable control over the floating-point environment, this document does not specify the effect on floating-point evaluation in constant expressions. — *end note*]

**2** The floating-point environment has thread storage duration ([[basic.stc.thread]](../basic.stc.thread.md)). The initial state for a thread’s floating-point environment is the state of the floating-point environment of the thread that constructs the corresponding `thread` object ([[thread.thread.class]](thread.thread.class.md)) or `jthread` object ([[thread.jthread.class]](thread.jthread.class.md)) at the time it constructed the object. [*Note 2*: That is, the child thread gets the floating-point state of the parent thread at the time of the child’s creation. — *end note*]

**3** A separate floating-point environment is maintained for each thread. Each function accesses the environment corresponding to its calling thread.

*See also:* ISO C 7.6
