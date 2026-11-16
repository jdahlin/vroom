# 16.5.5.10 Data race avoidance [res.on.data.races]

[Contents](../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.9 Reentrancy \[reentrancy\]](reentrancy.md) · next: [16.5.5.11 Protection within classes \[protection.within.classes\]](protection.within.classes.md)

**1** This subclause specifies requirements that implementations shall meet to prevent data races ([[intro.multithread]](../intro.multithread.md)). Every standard library function shall meet each requirement unless otherwise specified. Implementations may prevent data races in cases other than those specified below.

**2** A C++ standard library function shall not directly or indirectly access objects ([[intro.multithread]](../intro.multithread.md)) accessible by threads other than the current thread unless the objects are accessed directly or indirectly via the function’s arguments, including `this`.

**3** A C++ standard library function shall not directly or indirectly modify objects ([[intro.multithread]](../intro.multithread.md)) accessible by threads other than the current thread unless the objects are accessed directly or indirectly via the function’s non-const arguments, including `this`.

**4** [*Note 1*: This means, for example, that implementations can’t use an object with static storage duration for internal purposes without synchronization because it could cause a data race even in programs that do not explicitly share objects between threads. — *end note*]

**5** A C++ standard library function shall not access objects indirectly accessible via its arguments or via elements of its container arguments except by invoking functions required by its specification on those container elements.

**6** Operations on iterators obtained by calling a standard library container or string member function may access the underlying container, but shall not modify it. [*Note 2*: In particular, container operations that invalidate iterators conflict with operations on iterators associated with that container. — *end note*]

**7** Implementations may share their own internal objects between threads if the objects are not visible to users and are protected against data races.

**8** Unless otherwise specified, C++ standard library functions shall perform all operations solely within the current thread if those operations have effects that are visible ([[intro.multithread]](../intro.multithread.md)) to users.

**9** [*Note 3*: This allows implementations to parallelize operations if there are no visible side effects. — *end note*]
