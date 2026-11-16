# D.9.5 Header `<tgmath.h>` synopsis [depr.tgmath.h.syn]

[Contents](../README.md) · up: [D.9 C headers \[depr.c.headers\]](depr.c.headers.md) · prev: [D.9.4 Header `<stdbool.h>` synopsis \[depr.stdbool.h.syn\]](depr.stdbool.h.syn.md) · next: [D.9.6 Other C headers \[depr.c.headers.other\]](depr.c.headers.other.md)

```cpp
#include <cmath>
#include <complex>
```

**1** The header `<tgmath.h>` behaves as if it simply includes the headers `<cmath>` ([[cmath.syn]](../lib/numerics/cmath.syn.md)) and `<complex>` ([[complex.syn]](../lib/numerics/complex.syn.md)).

**2** [*Note 1*: The overloads provided in C by type-generic macros are already provided in `<complex>` and `<cmath>` by “sufficient” additional overloads. — *end note*]

**3** [*Note 2*: Names introduced by `<cmath>` or `<complex>` in namespace `std` are not placed into the global namespace scope by `<tgmath.h>`. — *end note*]
