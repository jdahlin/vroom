# 17.11.2.1 Preamble [cmp.categories.pre]

[Contents](../../README.md) · up: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md) · prev: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md) · next: [17.11.2.2 Class `partial_ordering` \[cmp.partialord\]](cmp.partialord.md)

**1** The types `partial_ordering`, `weak_ordering`, and `strong_ordering` are collectively termed the *comparison category types*. Each is specified in terms of an exposition-only data member named `value` whose value typically corresponds to that of an enumerator from one of the following exposition-only enumerations:

```cpp
enum class eq { equal = 0, equivalent = equal,
                nonequal = 1, nonequivalent = nonequal };   // exposition only
enum class ord { less = -1, greater = 1 };                  // exposition only
enum class ncmp { unordered = -127 };                       // exposition only
```

**2** [*Note 1*: The type `strong_ordering` corresponds to the term total ordering in mathematics. — *end note*]

**3** The relational and equality operators for the comparison category types are specified with an anonymous parameter of unspecified type. This type shall be selected by the implementation such that these parameters can accept literal `0` as a corresponding argument. [*Example 1*: `nullptr_t` meets this requirement. — *end example*] In this context, the behavior of a program that supplies an argument other than a literal `0` is undefined.

**4** For the purposes of subclause [[cmp.categories]](cmp.categories.md), *substitutability* is the property that `f(a) == f(b)` is `true` whenever `a == b` is `true`, where `f` denotes a function that reads only comparison-salient state that is accessible via the argument’s public const members.
