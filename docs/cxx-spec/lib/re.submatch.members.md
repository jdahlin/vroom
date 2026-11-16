# 30.9.1 Members [re.submatch.members]

[Contents](../README.md) · up: [30.9 Class template `sub_match` \[re.submatch\]](re.submatch.md) · prev: [30.9 Class template `sub_match` \[re.submatch\]](re.submatch.md) · next: [30.9.2 Non-member operators \[re.submatch.op\]](re.submatch.op.md)

```cpp
constexpr sub_match();
```

**1** *Effects:* Value-initializes the `pair` base class subobject and the member `matched`.

```cpp
difference_type length() const;
```

**2** *Returns:* `matched ? distance(first, second): 0`.

```cpp
operator string_type() const;
```

**3** *Returns:* `matched ? string_type(first, second): string_type()`.

```cpp
string_type str() const;
```

**4** *Returns:* `matched ? string_type(first, second): string_type()`.

```cpp
int compare(const sub_match& s) const;
```

**5** *Returns:* `str().compare(s.str())`.

```cpp
int compare(const string_type& s) const;
```

**6** *Returns:* `str().compare(s)`.

```cpp
int compare(const value_type* s) const;
```

**7** *Returns:* `str().compare(s)`.
