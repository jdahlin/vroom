# 6.9.3.3 Dynamic initialization of non-local variables [basic.start.dynamic]

[Contents](README.md) · up: [6.9.3 Start and termination \[basic.start\]](basic.start.md) · prev: [6.9.3.2 Static initialization \[basic.start.static\]](basic.start.static.md) · next: [6.9.3.4 Termination \[basic.start.term\]](basic.start.term.md)

**1** Dynamic initialization of a non-local variable with static storage duration is unordered if the variable is an implicitly or explicitly instantiated specialization, is partially-ordered if the variable is an inline variable that is not an implicitly or explicitly instantiated specialization, and otherwise is ordered. [*Note 1*: An explicitly specialized non-inline static data member or variable template specialization has ordered initialization. — *end note*]

**2** A declaration `D` is *appearance-ordered* before a declaration `E` if

- `D` appears in the same translation unit as `E`, or
- the translation unit containing `E` has an interface dependency on the translation unit containing `D`,

in either case prior to `E`.

**3** Dynamic initialization of non-local variables `V` and `W` with static storage duration are ordered as follows:

- If `V` and `W` have ordered initialization and the definition of `V` is appearance-ordered before the definition of `W`, or if `V` has partially-ordered initialization, `W` does not have unordered initialization, and for every definition `E` of `W` there exists a definition `D` of `V` such that `D` is appearance-ordered before `E`, then

  - if the program does not start a thread ([[intro.multithread]](intro.multithread.md)) other than the main thread ([[basic.start.main]](basic.start.main.md)) or `V` and `W` have ordered initialization and they are defined in the same translation unit, the initialization of `V` is sequenced before the initialization of `W`;
  - otherwise, the initialization of `V` strongly happens before the initialization of `W`.

- Otherwise, if the program starts a thread other than the main thread before either `V` or `W` is initialized, it is unspecified in which threads the initializations of `V` and `W` occur; the initializations are unsequenced if they occur in the same thread.

- Otherwise, the initializations of `V` and `W` are indeterminately sequenced.

[*Note 2*: This definition permits initialization of a sequence of ordered variables concurrently with another sequence. — *end note*]

**4** A *non-initialization odr-use* is an odr-use ([[basic.def.odr]](basic.def.odr.md)) not caused directly or indirectly by the initialization of a non-local static or thread storage duration variable.

**5** It is implementation-defined whether the dynamic initialization of a non-local non-inline variable with static storage duration is sequenced before the first statement of `main` or is deferred. If it is deferred, it strongly happens before any non-initialization odr-use of any non-inline function or non-inline variable defined in the same translation unit as the variable to be initialized.[^1] It is implementation-defined in which threads and at which points in the program such deferred dynamic initialization occurs. [*Note 3*: Such points should be chosen in a way that allows the programmer to avoid deadlocks. — *end note*] [*Example 1*:

```cpp
// - File 1 -
#include "a.h"
#include "b.h"
B b;
A::A(){
  b.Use();
}

// - File 2 -
#include "a.h"
A a;

// - File 3 -
#include "a.h"
#include "b.h"
extern A a;
extern B b;

int main() {
  a.Use();
  b.Use();
}
```

It is implementation-defined whether either `a` or `b` is initialized before `main` is entered or whether the initializations are delayed until `a` is first odr-used in `main`. In particular, if `a` is initialized before `main` is entered, it is not guaranteed that `b` will be initialized before it is odr-used by the initialization of `a`, that is, before `A::A` is called. If, however, `a` is initialized at some point after the first statement of `main`, `b` will be initialized prior to its use in `A::A`. — *end example*]

**6** It is implementation-defined whether the dynamic initialization of a non-local inline variable with static storage duration is sequenced before the first statement of `main` or is deferred. If it is deferred, it strongly happens before any non-initialization odr-use of that variable. It is implementation-defined in which threads and at which points in the program such deferred dynamic initialization occurs.

**7** It is implementation-defined whether the dynamic initialization of a non-local non-inline variable with thread storage duration is sequenced before the first statement of the initial function of a thread or is deferred. If it is deferred, the initialization associated with the entity for thread *t* is sequenced before the first non-initialization odr-use by *t* of any non-inline variable with thread storage duration defined in the same translation unit as the variable to be initialized. It is implementation-defined in which threads and at which points in the program such deferred dynamic initialization occurs.

**8** If the initialization of a non-local variable with static or thread storage duration exits via an exception, the function `std::terminate` is called ([[except.terminate]](except.terminate.md)).

[^1]: A non-local variable with static storage duration having initialization with side effects is initialized in this case, even if it is not itself odr-used ([[basic.def.odr]](basic.def.odr.md), [[basic.stc.static]](basic.stc.static.md)).
