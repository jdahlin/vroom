# 21.4.2.2 Iterator support [string.view.iterators]

[Contents](../../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.1 Construction and assignment \[string.view.cons\]](string.view.cons.md) · next: [21.4.2.3 Capacity \[string.view.capacity\]](string.view.capacity.md)

```cpp
using const_iterator = implementation-defined;
```

**1** A type that meets the requirements of a constant *Cpp17RandomAccessIterator* ([[random.access.iterators]](../iterators/random.access.iterators.md)), models `contiguous_iterator` ([[iterator.concept.contiguous]](../iterators/iterator.concept.contiguous.md)), and meets the constexpr iterator requirements ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)), whose `value_type` is the template parameter `charT`.

**2** All requirements on container iterators ([[container.requirements]](../containers/container.requirements.md)) apply to `basic_string_view::const_iterator` as well.

```cpp
constexpr const_iterator begin() const noexcept;
constexpr const_iterator cbegin() const noexcept;
```

**3** *Returns:* An iterator such that

- if `!empty()`, `addressof(*begin()) == data_`,
- otherwise, an unspecified value such that `[begin(), end())` is a valid range.

```cpp
constexpr const_iterator end() const noexcept;
constexpr const_iterator cend() const noexcept;
```

**4** *Returns:* `begin() + size()`.

```cpp
constexpr const_reverse_iterator rbegin() const noexcept;
constexpr const_reverse_iterator crbegin() const noexcept;
```

**5** *Returns:* `const_reverse_iterator(end())`.

```cpp
constexpr const_reverse_iterator rend() const noexcept;
constexpr const_reverse_iterator crend() const noexcept;
```

**6** *Returns:* `const_reverse_iterator(begin())`.
