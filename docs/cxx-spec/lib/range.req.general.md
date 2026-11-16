# 24.4.1 General [range.req.general]

[Contents](../README.md) · up: [24.4 Range requirements \[range.req\]](range.req.md) · prev: [24.4 Range requirements \[range.req\]](range.req.md) · next: [24.4.2 Ranges \[range.range\]](range.range.md)

**1** Ranges are an abstraction that allow a C++ program to operate on elements of data structures uniformly. Calling `ranges::begin` on a range returns an object whose type models `input_or_output_iterator` ([[iterator.concept.iterator]](iterator.concept.iterator.md)). Calling `ranges::end` on a range returns an object whose type `S`, together with the type `I` of the object returned by `ranges::begin`, models `sentinel_for<S, I>`. The library formalizes the interfaces, semantics, and complexity of ranges to enable algorithms and range adaptors that work efficiently on different types of sequences.

**2** The `range` concept requires that `ranges::begin` and `ranges::end` return an iterator and a sentinel, respectively. The `sized_range` concept refines `range` with the requirement that `ranges::size` be amortized 𝑂(1). The `view` concept specifies requirements on a `range` type with constant-time destruction and move operations.

**3** Several refinements of `range` group requirements that arise frequently in concepts and algorithms. Common ranges are ranges for which `ranges::begin` and `ranges::end` return objects of the same type. Random access ranges are ranges for which `ranges::begin` returns a type that models `random_access_iterator` ([[iterator.concept.random.access]](iterator.concept.random.access.md)). (Contiguous, bidirectional, forward, input, and output ranges are defined similarly.) Viewable ranges can be converted to views.
