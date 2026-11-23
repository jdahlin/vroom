# 27.8.4.3 Non-member functions [time.cal.month.nonmembers]

[Contents](../../README.md) · up: [27.8.4 Class `month` \[time.cal.month\]](time.cal.month.md) · prev: [27.8.4.2 Member functions \[time.cal.month.members\]](time.cal.month.members.md) · next: [27.8.5 Class `year` \[time.cal.year\]](time.cal.year.md)

```cpp
constexpr bool operator==(const month& x, const month& y) noexcept;
```

**1** *Returns:* `unsigned{x} == unsigned{y}`.

```cpp
constexpr strong_ordering operator<=>(const month& x, const month& y) noexcept;
```

**2** *Returns:* `unsigned{x} <=> unsigned{y}`.

```cpp
constexpr month operator+(const month& x, const months& y) noexcept;
```

**3** *Returns:*

```cpp
month{modulo(static_cast<long long>(unsigned{x}) + (y.count() - 1), 12) + 1}
```

where `modulo(n, 12)` computes the remainder of `n` divided by 12 using Euclidean division. [*Note 1*: Given a divisor of 12, Euclidean division truncates towards negative infinity and always produces a remainder in the range of `[0, 11]`. Assuming no overflow in the signed summation, this operation results in a `month` holding a value in the range `[1, 12]` even if `!x.ok()`. — *end note*] [*Example 1*: `February + months{11} == January`. — *end example*]

```cpp
constexpr month operator+(const months& x, const month& y) noexcept;
```

**4** *Returns:* `y + x`.

```cpp
constexpr month operator-(const month& x, const months& y) noexcept;
```

**5** *Returns:* `x + -y`.

```cpp
constexpr months operator-(const month& x, const month& y) noexcept;
```

**6** *Returns:* If `x.ok() == true` and `y.ok() == true`, returns a value `m` in the range `[months{0}, months{11}]` satisfying `y + m == x`. Otherwise the value returned is unspecified. [*Example 2*: `January - February == months{11}`. — *end example*]

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const month& m);
```

**7** *Effects:* Equivalent to:

```cpp
return os << (m.ok() ?
  format(os.getloc(), STATICALLY-WIDEN<charT>("{:%b}"), m) :
  format(os.getloc(), STATICALLY-WIDEN<charT>("{} is not a valid month"),
         static_cast<unsigned>(m)));
```

```cpp
template<class charT, class traits, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                month& m, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**8** *Effects:* Attempts to parse the input stream `is` into the `month` `m` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse fails to decode a valid month, `is.setstate(ios_base::failbit)` is called and `m` is not modified. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**9** *Returns:* `is`.
