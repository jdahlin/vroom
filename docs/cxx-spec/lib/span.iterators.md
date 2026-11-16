# 22.7.3.7 Iterator support [span.iterators]

[Contents](../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.6 Element access \[span.elem\]](span.elem.md) · next: [22.7.3.8 Views of object representation \[span.objectrep\]](span.objectrep.md)

```cpp
using iterator = implementation-defined;
```

**1** The type models `contiguous_iterator` ([[iterator.concept.contiguous]](iterator.concept.contiguous.md)), meets the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](random.access.iterators.md)), and meets the requirements for constexpr iterators ([[iterator.requirements.general]](iterator.requirements.general.md)). All requirements on container iterators ([[container.requirements]](container.requirements.md)) apply to `span::iterator` as well.

```cpp
constexpr iterator begin() const noexcept;
```

**2** *Returns:* An iterator referring to the first element in the span. If `empty()` is `true`, then it returns the same value as `end()`.

```cpp
constexpr iterator end() const noexcept;
```

**3** *Returns:* An iterator which is the past-the-end value.

```cpp
constexpr reverse_iterator rbegin() const noexcept;
```

**4** *Effects:* Equivalent to: `return reverse_iterator(end());`

```cpp
constexpr reverse_iterator rend() const noexcept;
```

**5** *Effects:* Equivalent to: `return reverse_iterator(begin());`
