# 21.4.2.4 Element access [string.view.access]

[Contents](../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.3 Capacity \[string.view.capacity\]](string.view.capacity.md) · next: [21.4.2.5 Modifiers \[string.view.modifiers\]](string.view.modifiers.md)

```cpp
constexpr const_reference operator[](size_type pos) const;
```

**1** *Preconditions:* `pos < size()`.

**2** *Returns:* `data_[pos]`.

**3** *Throws:* Nothing.

**4** [*Note 1*: Unlike `basic_string::operator[]`, `basic_string_view::operator[](size())` has undefined behavior instead of returning `charT()`. — *end note*]

```cpp
constexpr const_reference at(size_type pos) const;
```

**5** *Throws:* `out_of_range` if `pos >= size()`.

**6** *Returns:* `data_[pos]`.

```cpp
constexpr const_reference front() const;
```

**7** *Preconditions:* `!empty()`.

**8** *Returns:* `data_[0]`.

**9** *Throws:* Nothing.

```cpp
constexpr const_reference back() const;
```

**10** *Preconditions:* `!empty()`.

**11** *Returns:* `data_[size() - 1]`.

**12** *Throws:* Nothing.

```cpp
constexpr const_pointer data() const noexcept;
```

**13** *Returns:* `data_`.

**14** [*Note 2*: Unlike `basic_string::data()` and *string-literal*s, `data()` may return a pointer to a buffer that is not null-terminated. Therefore it is typically a mistake to pass `data()` to a function that takes just a `const charT*` and expects a null-terminated string. — *end note*]
