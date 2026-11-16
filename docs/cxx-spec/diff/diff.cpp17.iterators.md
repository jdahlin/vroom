# C.1.11 [[iterators]](../lib/iterators/iterators.md): iterators library [diff.cpp17.iterators]

[Contents](../README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.10 [[containers]](../lib/containers/containers.md): containers library \[diff.cpp17.containers\]](diff.cpp17.containers.md) · next: [C.1.12 [[algorithms]](../lib/algorithms/algorithms.md): algorithms library \[diff.cpp17.alg.reqs\]](diff.cpp17.alg.reqs.md)

**1** **Affected subclause:** [[iterator.traits]](../lib/iterators/iterator.traits.md) <br>**Change:** The specialization of `iterator_traits` for `void*` and for function pointer types no longer contains any nested typedefs. <br>**Rationale:** Corrects an issue misidentifying pointer types that are not incrementable as iterator types. <br>**Effect on original feature:** A valid C++ 2017 program that relies on the presence of the typedefs may fail to compile, or have different behavior.
