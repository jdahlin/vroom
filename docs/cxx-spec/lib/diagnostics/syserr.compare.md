# 19.5.5 Comparison functions [syserr.compare]

[Contents](../../README.md) · up: [19.5 System error support \[syserr\]](syserr.md) · prev: [19.5.4.5 Non-member functions \[syserr.errcondition.nonmembers\]](syserr.errcondition.nonmembers.md) · next: [19.5.6 System error hash support \[syserr.hash\]](syserr.hash.md)

```cpp
bool operator==(const error_code& lhs, const error_code& rhs) noexcept;
```

**1** *Returns:*

```cpp
lhs.category() == rhs.category() && lhs.value() == rhs.value()
```

```cpp
bool operator==(const error_code& lhs, const error_condition& rhs) noexcept;
```

**2** *Returns:*

```cpp
lhs.category().equivalent(lhs.value(), rhs) || rhs.category().equivalent(lhs, rhs.value())
```

```cpp
bool operator==(const error_condition& lhs, const error_condition& rhs) noexcept;
```

**3** *Returns:*

```cpp
lhs.category() == rhs.category() && lhs.value() == rhs.value()
```

```cpp
strong_ordering operator<=>(const error_code& lhs, const error_code& rhs) noexcept;
```

**4** *Effects:* Equivalent to:

```cpp
if (auto c = lhs.category() <=> rhs.category(); c != 0) return c;
return lhs.value() <=> rhs.value();
```

```cpp
strong_ordering operator<=>(const error_condition& lhs, const error_condition& rhs) noexcept;
```

**5** *Returns:*

```cpp
if (auto c = lhs.category() <=> rhs.category(); c != 0) return c;
return lhs.value() <=> rhs.value();
```
