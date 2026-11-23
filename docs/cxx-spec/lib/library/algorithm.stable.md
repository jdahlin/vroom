# 16.5.5.8 Requirements for stable algorithms [algorithm.stable]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.7 Constexpr functions and constructors \[constexpr.functions\]](constexpr.functions.md) · next: [16.5.5.9 Reentrancy \[reentrancy\]](reentrancy.md)

**1** When the requirements for an algorithm state that it is “stable” without further elaboration, it means:

- For the sort algorithms the relative order of equivalent elements is preserved.
- For the remove and copy algorithms the relative order of the elements that are not removed is preserved.
- For the merge algorithms, for equivalent elements in the original two ranges, the elements from the first range (preserving their original order) precede the elements from the second range (preserving their original order).
