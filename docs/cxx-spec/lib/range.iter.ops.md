# 23.4.3 Range iterator operations [range.iter.ops]

[Contents](../README.md) · up: [23.4 Iterator primitives \[iterator.primitives\]](iterator.primitives.md) · prev: [23.4.2 Iterator operations \[iterator.operations\]](iterator.operations.md) · next: [23.4.3.1 `ranges::advance` \[range.iter.op.advance\]](range.iter.op.advance.md)

**1** The library includes the function templates `ranges::advance`, `ranges::distance`, `ranges::next`, and `ranges::prev` to manipulate iterators. These operations adapt to the set of operators provided by each iterator category to provide the most efficient implementation possible for a concrete iterator type. [*Example 1*: `ranges::advance` uses the `+` operator to move a `random_access_iterator` forward `n` steps in constant time. For an iterator type that does not model `random_access_iterator`, `ranges::advance` instead performs `n` individual increments with the `++` operator. — *end example*]

**2** The function templates defined in this subclause are not found by argument-dependent name lookup ([[basic.lookup.argdep]](../basic.lookup.argdep.md)). When found by unqualified ([[basic.lookup.unqual]](../basic.lookup.unqual.md)) name lookup for the *postfix-expression* in a function call ([[expr.call]](../expr.call.md)), they inhibit argument-dependent name lookup.

[*Example 2*:

```cpp
void foo() {
    using namespace std::ranges;
    std::vector<int> vec{1,2,3};
    distance(begin(vec), end(vec));     // #1
}
```

The function call expression at `#1` invokes `std::ranges::distance`, not `std::distance`, despite that (a) the iterator type returned from `begin(vec)` and `end(vec)` may be associated with namespace `std` and (b) `std::distance` is more specialized ([[temp.func.order]](../temp.func.order.md)) than `std::ranges::distance` since the former requires its first two parameters to have the same type. — *end example*]

**3** The number and order of deducible template parameters for the function templates defined in this subclause is unspecified, except where explicitly stated otherwise.

## Subclauses

- [23.4.3.1 `ranges::advance` \[range.iter.op.advance\]](range.iter.op.advance.md)
- [23.4.3.2 `ranges::distance` \[range.iter.op.distance\]](range.iter.op.distance.md)
- [23.4.3.3 `ranges::next` \[range.iter.op.next\]](range.iter.op.next.md)
- [23.4.3.4 `ranges::prev` \[range.iter.op.prev\]](range.iter.op.prev.md)
