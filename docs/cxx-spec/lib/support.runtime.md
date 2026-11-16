# 17.13 Other runtime support [support.runtime]

[Contents](../README.md) · up: [17 Language support library \[support\]](support.md) · prev: [17.12.5 Trivial awaitables \[coroutine.trivial.awaitables\]](coroutine.trivial.awaitables.md) · next: [17.13.1 Header `<cstdarg>` synopsis \[cstdarg.syn\]](cstdarg.syn.md)

**1** Headers `<csetjmp>` (nonlocal jumps), `<csignal>` (signal handling), `<cstdarg>` (variable arguments), and `<cstdlib>` (runtime environment `getenv`, `system`), provide further compatibility with C code.

**2** Calls to the function `getenv` ([[cstdlib.syn]](cstdlib.syn.md)) shall not introduce a data race ([[res.on.data.races]](res.on.data.races.md)) provided that nothing modifies the environment. [*Note 1*: Calls to the POSIX functions `setenv` and `putenv` modify the environment. — *end note*]

**3** A call to the `setlocale` function ([[c.locales]](c.locales.md)) may introduce a data race with other calls to the `setlocale` function or with calls to functions that are affected by the current C locale. The implementation shall behave as if no library function other than `locale::global` calls the `setlocale` function.

## Subclauses

- [17.13.1 Header `<cstdarg>` synopsis \[cstdarg.syn\]](cstdarg.syn.md)
- [17.13.2 Header `<csetjmp>` synopsis \[csetjmp.syn\]](csetjmp.syn.md)
- [17.13.3 Header `<csignal>` synopsis \[csignal.syn\]](csignal.syn.md)
- [17.13.4 Signal handlers \[support.signal\]](support.signal.md)
