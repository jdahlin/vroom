# 22.7.3.4 Subviews [span.sub]

[Contents](../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.3 Deduction guides \[span.deduct\]](span.deduct.md) · next: [22.7.3.5 Observers \[span.obs\]](span.obs.md)

```cpp
template<size_t Count> constexpr span<element_type, Count> first() const;
```

**1** *Mandates:* `Count <= Extent` is `true`.

**2** *Preconditions:* `Count <= size()` is `true`.

**3** *Effects:* Equivalent to: `return R{data(), Count};` where `R` is the return type.

```cpp
template<size_t Count> constexpr span<element_type, Count> last() const;
```

**4** *Mandates:* `Count <= Extent` is `true`.

**5** *Preconditions:* `Count <= size()` is `true`.

**6** *Effects:* Equivalent to: `return R{data() + (size() - Count), Count};` where `R` is the return type.

```cpp
template<size_t Offset, size_t Count = dynamic_extent>
  constexpr span<element_type, see below> subspan() const;
```

**7** *Mandates:*

```cpp
Offset <= Extent && (Count == dynamic_extent || Count <= Extent - Offset)
```

is `true`.

**8** *Preconditions:*

```cpp
Offset <= size() && (Count == dynamic_extent || Count <= size() - Offset)
```

is `true`.

**9** *Effects:* Equivalent to:

```cpp
return span<ElementType, see below>(
  data() + Offset, Count != dynamic_extent ? Count : size() - Offset);
```

**10** *Remarks:* The second template argument of the returned `span` type is:

```cpp
Count != dynamic_extent ? Count
                        : (Extent != dynamic_extent ? Extent - Offset
                                                    : dynamic_extent)
```

```cpp
constexpr span<element_type, dynamic_extent> first(size_type count) const;
```

**11** *Preconditions:* `count <= size()` is `true`.

**12** *Effects:* Equivalent to: `return {data(), count};`

```cpp
constexpr span<element_type, dynamic_extent> last(size_type count) const;
```

**13** *Preconditions:* `count <= size()` is `true`.

**14** *Effects:* Equivalent to: `return {data() + (size() - count), count};`

```cpp
constexpr span<element_type, dynamic_extent> subspan(
  size_type offset, size_type count = dynamic_extent) const;
```

**15** *Preconditions:*

```cpp
offset <= size() && (count == dynamic_extent || count <= size() - offset)
```

is `true`.

**16** *Effects:* Equivalent to:

```cpp
return {data() + offset, count == dynamic_extent ? size() - offset : count};
```
