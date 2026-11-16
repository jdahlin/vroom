# 17.13.3 Header `<csignal>` synopsis [csignal.syn]

[Contents](../README.md) · up: [17.13 Other runtime support \[support.runtime\]](support.runtime.md) · prev: [17.13.2 Header `<csetjmp>` synopsis \[csetjmp.syn\]](csetjmp.syn.md) · next: [17.13.4 Signal handlers \[support.signal\]](support.signal.md)

```cpp
namespace std {
  using sig_atomic_t = see below;

  // [support.signal], signal handlers
  extern "C" using signal-handler = void(int);  // exposition only
  signal-handler* signal(int sig, signal-handler* func);

  int raise(int sig);
}

#define SIG_DFL see below
#define SIG_ERR see below
#define SIG_IGN see below
#define SIGABRT see below
#define SIGFPE see below
#define SIGILL see below
#define SIGINT see below
#define SIGSEGV see below
#define SIGTERM see below
```

**1** The contents of the header `<csignal>` are the same as the C standard library header `<signal.h>`.
