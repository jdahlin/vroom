# 25.10.2 Accumulate [accumulate]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.1 Definitions \[numerics.defns\]](numerics.defns.md) · next: [25.10.3 Reduce \[reduce\]](reduce.md)

```cpp
template<class InputIterator, class T>
  constexpr T accumulate(InputIterator first, InputIterator last, T init);
template<class InputIterator, class T, class BinaryOperation>
  constexpr T accumulate(InputIterator first, InputIterator last, T init,
                         BinaryOperation binary_op);
```

**1** *Preconditions:* `T` meets the *Cpp17CopyConstructible* ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)) and *Cpp17CopyAssignable* ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)) requirements. In the range `[first, last]`, `binary_op` neither modifies elements nor invalidates iterators or subranges.[^1]

**2** *Effects:* Computes its result by initializing the accumulator `acc` with the initial value `init` and then modifies it with `acc = std::move(acc) + *i` or `acc = binary_op(std::move(acc), *i)` for every iterator `i` in the range `[first, last)` in order.[^2]

[^1]: The use of fully closed ranges is intentional.
[^2]: `accumulate` is similar to the APL reduction operator and Common Lisp reduce function, but it avoids the difficulty of defining the result of reduction on an empty sequence by always requiring an initial value.
