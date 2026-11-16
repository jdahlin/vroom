# 26.5.3 Function template `bit_cast` [bit.cast]

[Contents](../../README.md) · up: [26.5 Bit manipulation \[bit\]](bit.md) · prev: [26.5.2 Header `<bit>` synopsis \[bit.syn\]](bit.syn.md) · next: [26.5.4 Integral powers of 2 \[bit.pow.two\]](bit.pow.two.md)

```cpp
template<class To, class From>
  constexpr To bit_cast(const From& from) noexcept;
```

**1** *Constraints:*

- `sizeof(To) == sizeof(From)` is `true`;
- `is_trivially_copyable_v<To>` is `true`; and
- `is_trivially_copyable_v<From>` is `true`.

**2** *Returns:* An object of type `To`. Implicitly creates objects nested within the result ([[intro.object]](../../basic/intro.object.md)). Each bit of the value representation of the result is equal to the corresponding bit in the object representation of `from`. Padding bits of the result are unspecified. For the result and each object created within it, if there is no value of the object’s type corresponding to the value representation produced, the behavior is undefined. If there are multiple such values, which value is produced is unspecified.

**3** *Remarks:* This function is `constexpr` if and only if `To`, `From`, and the types of all subobjects of `To` and `From` are types `T` such that:

- `is_union_v<T>` is `false`;
- `is_pointer_v<T>` is `false`;
- `is_member_pointer_v<T>` is `false`;
- `is_volatile_v<T>` is `false`; and
- `T` has no non-static data members of reference type.
