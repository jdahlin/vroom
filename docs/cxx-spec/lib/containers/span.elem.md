# 22.7.3.6 Element access [span.elem]

[Contents](../../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.5 Observers \[span.obs\]](span.obs.md) · next: [22.7.3.7 Iterator support \[span.iterators\]](span.iterators.md)

```cpp
constexpr reference operator[](size_type idx) const;
```

**1** *Preconditions:* `idx < size()` is `true`.

**2** *Effects:* Equivalent to: `return *(data() + idx);`

```cpp
constexpr reference front() const;
```

**3** *Preconditions:* `empty()` is `false`.

**4** *Effects:* Equivalent to: `return *data();`

```cpp
constexpr reference back() const;
```

**5** *Preconditions:* `empty()` is `false`.

**6** *Effects:* Equivalent to: `return *(data() + (size() - 1));`

```cpp
constexpr pointer data() const noexcept;
```

**7** *Effects:* Equivalent to: `return data_;`
