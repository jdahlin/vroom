# 25.2 Algorithms requirements [algorithms.requirements]

[Contents](../README.md) · up: [25 Algorithms library \[algorithms\]](algorithms.md) · prev: [25.1 General \[algorithms.general\]](algorithms.general.md) · next: [25.3 Parallel algorithms \[algorithms.parallel\]](algorithms.parallel.md)

**1** All of the algorithms are separated from the particular implementations of data structures and are parameterized by iterator types. Because of this, they can work with program-defined data structures, as long as these data structures have iterator types satisfying the assumptions on the algorithms.

**2** The entities defined in the `std::ranges` namespace in this Clause are not found by argument-dependent name lookup ([[basic.lookup.argdep]](../basic.lookup.argdep.md)). When found by unqualified ([[basic.lookup.unqual]](../basic.lookup.unqual.md)) name lookup for the *postfix-expression* in a function call ([[expr.call]](../expr.call.md)), they inhibit argument-dependent name lookup.

[*Example 1*:

```cpp
void foo() {
  using namespace std::ranges;
  std::vector<int> vec{1,2,3};
  find(begin(vec), end(vec), 2);        // #1
}
```

The function call expression at `#1` invokes `std::ranges::find`, not `std::find`, despite that (a) the iterator type returned from `begin(vec)` and `end(vec)` may be associated with namespace `std` and (b) `std::find` is more specialized ([[temp.func.order]](../temp.func.order.md)) than `std::ranges::find` since the former requires its first two parameters to have the same type. — *end example*]

**3** For purposes of determining the existence of data races, algorithms shall not modify objects referenced through an iterator argument unless the specification requires such modification.

**4** Throughout this Clause, where the template parameters are not constrained, the names of template parameters are used to express type requirements.

- If an algorithm’s template parameter is named `InputIterator`, `InputIterator1`, or `InputIterator2`, the template argument shall meet the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)).
- If an algorithm’s template parameter is named `OutputIterator`, `OutputIterator1`, or `OutputIterator2`, the template argument shall meet the *Cpp17OutputIterator* requirements ([[output.iterators]](output.iterators.md)).
- If an algorithm’s template parameter is named `ForwardIterator`, `ForwardIterator1`, or `ForwardIterator2`, the template argument shall meet the *Cpp17ForwardIterator* requirements ([[forward.iterators]](forward.iterators.md)).
- If an algorithm’s template parameter is named `NoThrowForwardIterator`, the template argument shall meet the *Cpp17ForwardIterator* requirements ([[forward.iterators]](forward.iterators.md)), and is required to have the property that no exceptions are thrown from increment, assignment, or comparison of, or indirection through, valid iterators.
- If an algorithm’s template parameter is named `BidirectionalIterator`, `BidirectionalIterator1`, or `BidirectionalIterator2`, the template argument shall meet the *Cpp17BidirectionalIterator* requirements ([[bidirectional.iterators]](bidirectional.iterators.md)).
- If an algorithm’s template parameter is named `RandomAccessIterator`, `RandomAccessIterator1`, or `RandomAccessIterator2`, the template argument shall meet the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](random.access.iterators.md)).

**5** If an algorithm’s *Effects:* element specifies that a value pointed to by any iterator passed as an argument is modified, then that algorithm has an additional type requirement: The type of that argument shall meet the requirements of a mutable iterator ([[iterator.requirements]](iterator.requirements.md)). [*Note 1*: This requirement does not affect arguments that are named `OutputIterator`, `OutputIterator1`, or `OutputIterator2`, because output iterators must always be mutable, nor does it affect arguments that are constrained, for which mutability requirements are expressed explicitly. — *end note*]

**6** Both in-place and copying versions are provided for certain algorithms.[^1] When such a version is provided for *algorithm* it is called *algorithm`_copy`*. Algorithms that take predicates end with the suffix `_if` (which follows the suffix `_copy`).

**7** When not otherwise constrained, the `Predicate` parameter is used whenever an algorithm expects a function object ([[function.objects]](function.objects.md)) that, when applied to the result of dereferencing the corresponding iterator, returns a value testable as `true`. In other words, if an algorithm takes `Predicate pred` as its argument and `first` as its iterator argument with value type `T`, it should work correctly in the construct `pred(*first)` contextually converted to `bool` ([[conv]](../conv.md)). The function object `pred` shall not apply any non-constant function through the dereferenced iterator. Given a glvalue `u` of type (possibly `const`) `T` that designates the same object as `*first`, `pred(u)` shall be a valid expression that is equal to `pred(*first)`.

**8** When not otherwise constrained, the `BinaryPredicate` parameter is used whenever an algorithm expects a function object that when applied to the result of dereferencing two corresponding iterators or to dereferencing an iterator and type `T` when `T` is part of the signature returns a value testable as `true`. In other words, if an algorithm takes `BinaryPredicate binary_pred` as its argument and `first1` and `first2` as its iterator arguments with respective value types `T1` and `T2`, it should work correctly in the construct `binary_pred(*first1, *first2)` contextually converted to `bool` ([[conv]](../conv.md)). Unless otherwise specified, `BinaryPredicate` always takes the first iterator’s `value_type` as its first argument, that is, in those cases when `T value` is part of the signature, it should work correctly in the construct `binary_pred(*first1, value)` contextually converted to `bool` ([[conv]](../conv.md)). `binary_pred` shall not apply any non-constant function through the dereferenced iterators. Given a glvalue `u` of type (possibly `const`) `T1` that designates the same object as `*first1`, and a glvalue `v` of type (possibly `const`) `T2` that designates the same object as `*first2`, `binary_pred(u, *first2)`, `binary_pred(*first1, v)`, and `binary_pred(u, v)` shall each be a valid expression that is equal to `binary_pred(*first1, *first2)`, and `binary_pred(u, value)` shall be a valid expression that is equal to `binary_pred(*first1, value)`.

**9** The parameters `UnaryOperation`, `BinaryOperation`, `BinaryOperation1`, and `BinaryOperation2` are used whenever an algorithm expects a function object ([[function.objects]](function.objects.md)).

**10** [*Note 2*: Unless otherwise specified, algorithms that take function objects as arguments are permitted to copy those function objects freely. Programmers for whom object identity is important should consider using a wrapper class that points to a noncopied implementation object such as `reference_wrapper<T>` ([[refwrap]](refwrap.md)), or some equivalent solution. — *end note*]

**11** When the description of an algorithm gives an expression such as `*first == value` for a condition, the expression shall evaluate to either `true` or `false` in boolean contexts.

**12** In the description of the algorithms, operator `+` is used for some of the iterator categories for which it does not have to be defined. In these cases the semantics of `a + n` are the same as those of

```cpp
auto tmp = a;
for (; n < 0; ++n) --tmp;
for (; n > 0; --n) ++tmp;
return tmp;
```

Similarly, operator `-` is used for some combinations of iterators and sentinel types for which it does not have to be defined. If `[a, b)` denotes a range, the semantics of `b - a` in these cases are the same as those of

```cpp
iter_difference_t<decltype(a)> n = 0;
for (auto tmp = a; tmp != b; ++tmp) ++n;
return n;
```

and if `[b, a)` denotes a range, the same as those of

```cpp
iter_difference_t<decltype(b)> n = 0;
for (auto tmp = b; tmp != a; ++tmp) --n;
return n;
```

**13** In the description of algorithm return values, a sentinel value `s` denoting the end of a range `[i, s)` is sometimes returned where an iterator is expected. In these cases, the semantics are as if the sentinel is converted into an iterator using `ranges::next(i, s)`.

**14** Overloads of algorithms that take `range` arguments ([[range.range]](range.range.md)) behave as if they are implemented by calling `ranges::begin` and `ranges::end` on the `range`(s) and dispatching to the overload in namespace `ranges` that takes separate iterator and sentinel arguments.

**15** The number and order of deducible template parameters for algorithm declarations are unspecified, except where explicitly stated otherwise. [*Note 3*: Consequently, the algorithms may not be called with explicitly-specified template argument lists. — *end note*]

[^1]: The decision whether to include a copying version was usually based on complexity considerations. When the cost of doing the operation dominates the cost of copy, the copying version is not included. For example, `sort_copy` is not included because the cost of sorting is much more significant, and users might as well do `copy` followed by `sort`.
