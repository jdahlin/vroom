# 20.15.10 Constant evaluation context [meta.const.eval]

[Contents](../../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.9 Member relationships \[meta.member\]](meta.member.md) · next: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md)

```cpp
constexpr bool is_constant_evaluated() noexcept;
```

**1** *Returns:* `true` if and only if evaluation of the call occurs within the evaluation of an expression or conversion that is manifestly constant-evaluated ([[expr.const]](../../expr/expr.const.md)).

**2** [*Example 1*:

```cpp
constexpr void f(unsigned char *p, int n) {
  if (std::is_constant_evaluated()) {           // should not be a constexpr if statement
    for (int k = 0; k<n; ++k) p[k] = 0;
  } else {
    memset(p, 0, n);                            // not a core constant expression
  }
}
```

— *end example*]
