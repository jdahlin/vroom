# 17.13.2 Header `<csetjmp>` synopsis [csetjmp.syn]

[Contents](../README.md) · up: [17.13 Other runtime support \[support.runtime\]](support.runtime.md) · prev: [17.13.1 Header `<cstdarg>` synopsis \[cstdarg.syn\]](cstdarg.syn.md) · next: [17.13.3 Header `<csignal>` synopsis \[csignal.syn\]](csignal.syn.md)

```cpp
namespace std {
  using jmp_buf = see below;
  [[noreturn]] void longjmp(jmp_buf env, int val);
}

#define setjmp(env) see below
```

**1** The contents of the header `<csetjmp>` are the same as the C standard library header `<setjmp.h>`.

**2** The function signature `longjmp(jmp_buf jbuf, int val)` has more restricted behavior in this document. A `setjmp`/`longjmp` call pair has undefined behavior if replacing the `setjmp` and `longjmp` by `catch` and `throw` would invoke any non-trivial destructors for any objects with automatic storage duration. A call to `setjmp` or `longjmp` has undefined behavior if invoked in a suspension context of a coroutine ([[expr.await]](../expr.await.md)).

*See also:* ISO C 7.13
