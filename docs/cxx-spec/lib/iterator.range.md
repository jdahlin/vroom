# 23.7 Range access [iterator.range]

[Contents](../README.md) · up: [23 Iterators library \[iterators\]](iterators.md) · prev: [23.6.4.2 Operations \[ostreambuf.iter.ops\]](ostreambuf.iter.ops.md) · next: [24 Ranges library \[ranges\]](ranges.md)

**1** In addition to being available via inclusion of the `<iterator>` header, the function templates in [[iterator.range]](iterator.range.md) are available when any of the following headers are included: `<array>` ([[array.syn]](array.syn.md)), `<deque>` ([[deque.syn]](deque.syn.md)), `<forward_list>` ([[forward.list.syn]](forward.list.syn.md)), `<list>` ([[list.syn]](list.syn.md)), `<map>` ([[associative.map.syn]](associative.map.syn.md)), `<regex>` ([[re.syn]](re.syn.md)), `<set>` ([[associative.set.syn]](associative.set.syn.md)), `<span>` ([[span.syn]](span.syn.md)), `<string>` ([[string.syn]](string.syn.md)), `<string_view>` ([[string.view.synop]](string.view.synop.md)), `<unordered_map>` ([[unord.map.syn]](unord.map.syn.md)), `<unordered_set>` ([[unord.set.syn]](unord.set.syn.md)), and `<vector>` ([[vector.syn]](vector.syn.md)). Each of these templates is a designated customization point ([[namespace.std]](namespace.std.md)).

```cpp
template<class C> constexpr auto begin(C& c) -> decltype(c.begin());
template<class C> constexpr auto begin(const C& c) -> decltype(c.begin());
```

**2** *Returns:* `c.begin()`.

```cpp
template<class C> constexpr auto end(C& c) -> decltype(c.end());
template<class C> constexpr auto end(const C& c) -> decltype(c.end());
```

**3** *Returns:* `c.end()`.

```cpp
template<class T, size_t N> constexpr T* begin(T (&array)[N]) noexcept;
```

**4** *Returns:* `array`.

```cpp
template<class T, size_t N> constexpr T* end(T (&array)[N]) noexcept;
```

**5** *Returns:* `array + N`.

```cpp
template<class C> constexpr auto cbegin(const C& c) noexcept(noexcept(std::begin(c)))
  -> decltype(std::begin(c));
```

**6** *Returns:* `std::begin(c)`.

```cpp
template<class C> constexpr auto cend(const C& c) noexcept(noexcept(std::end(c)))
  -> decltype(std::end(c));
```

**7** *Returns:* `std::end(c)`.

```cpp
template<class C> constexpr auto rbegin(C& c) -> decltype(c.rbegin());
template<class C> constexpr auto rbegin(const C& c) -> decltype(c.rbegin());
```

**8** *Returns:* `c.rbegin()`.

```cpp
template<class C> constexpr auto rend(C& c) -> decltype(c.rend());
template<class C> constexpr auto rend(const C& c) -> decltype(c.rend());
```

**9** *Returns:* `c.rend()`.

```cpp
template<class T, size_t N> constexpr reverse_iterator<T*> rbegin(T (&array)[N]);
```

**10** *Returns:* `reverse_iterator<T*>(array + N)`.

```cpp
template<class T, size_t N> constexpr reverse_iterator<T*> rend(T (&array)[N]);
```

**11** *Returns:* `reverse_iterator<T*>(array)`.

```cpp
template<class E> constexpr reverse_iterator<const E*> rbegin(initializer_list<E> il);
```

**12** *Returns:* `reverse_iterator<const E*>(il.end())`.

```cpp
template<class E> constexpr reverse_iterator<const E*> rend(initializer_list<E> il);
```

**13** *Returns:* `reverse_iterator<const E*>(il.begin())`.

```cpp
template<class C> constexpr auto crbegin(const C& c) -> decltype(std::rbegin(c));
```

**14** *Returns:* `std::rbegin(c)`.

```cpp
template<class C> constexpr auto crend(const C& c) -> decltype(std::rend(c));
```

**15** *Returns:* `std::rend(c)`.

```cpp
template<class C> constexpr auto size(const C& c) -> decltype(c.size());
```

**16** *Returns:* `c.size()`.

```cpp
template<class T, size_t N> constexpr size_t size(const T (&array)[N]) noexcept;
```

**17** *Returns:* `N`.

```cpp
template<class C> constexpr auto ssize(const C& c)
  -> common_type_t<ptrdiff_t, make_signed_t<decltype(c.size())>>;
```

**18** *Effects:* Equivalent to:

```cpp
return static_cast<common_type_t<ptrdiff_t, make_signed_t<decltype(c.size())>>>(c.size());
```

```cpp
template<class T, ptrdiff_t N> constexpr ptrdiff_t ssize(const T (&array)[N]) noexcept;
```

**19** *Returns:* `N`.

```cpp
template<class C> [[nodiscard]] constexpr auto empty(const C& c) -> decltype(c.empty());
```

**20** *Returns:* `c.empty()`.

```cpp
template<class T, size_t N> [[nodiscard]] constexpr bool empty(const T (&array)[N]) noexcept;
```

**21** *Returns:* `false`.

```cpp
template<class E> [[nodiscard]] constexpr bool empty(initializer_list<E> il) noexcept;
```

**22** *Returns:* `il.size() == 0`.

```cpp
template<class C> constexpr auto data(C& c) -> decltype(c.data());
template<class C> constexpr auto data(const C& c) -> decltype(c.data());
```

**23** *Returns:* `c.data()`.

```cpp
template<class T, size_t N> constexpr T* data(T (&array)[N]) noexcept;
```

**24** *Returns:* `array`.

```cpp
template<class E> constexpr const E* data(initializer_list<E> il) noexcept;
```

**25** *Returns:* `il.begin()`.
