# 25.10.4 Inner product [inner.product]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.3 Reduce \[reduce\]](reduce.md) · next: [25.10.5 Transform reduce \[transform.reduce\]](transform.reduce.md)

```cpp
template<class InputIterator1, class InputIterator2, class T>
  constexpr T inner_product(InputIterator1 first1, InputIterator1 last1,
                            InputIterator2 first2, T init);
template<class InputIterator1, class InputIterator2, class T,
         class BinaryOperation1, class BinaryOperation2>
  constexpr T inner_product(InputIterator1 first1, InputIterator1 last1,
                            InputIterator2 first2, T init,
                            BinaryOperation1 binary_op1,
                            BinaryOperation2 binary_op2);
```

**1** *Preconditions:* `T` meets the *Cpp17CopyConstructible* ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)) and *Cpp17CopyAssignable* ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)) requirements. In the ranges `[first1, last1]` and `[first2, first2 + (last1 - first1)]` `binary_op1` and `binary_op2` neither modifies elements nor invalidates iterators or subranges.[^1]

**2** *Effects:* Computes its result by initializing the accumulator `acc` with the initial value `init` and then modifying it with `acc = std::move(acc) + (*i1) * (*i2)` or `acc = binary_op1(std::move(acc), binary_op2(*i1, *i2))` for every iterator `i1` in the range `[first1, last1)` and iterator `i2` in the range `[first2, first2 + (last1 - first1))` in order.

[^1]: The use of fully closed ranges is intentional.
