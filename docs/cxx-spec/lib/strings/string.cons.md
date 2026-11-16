# 21.3.2.2 Constructors and assignment operators [string.cons]

[Contents](../../README.md) · up: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · prev: [21.3.2.1 General requirements \[string.require\]](string.require.md) · next: [21.3.2.3 Iterator support \[string.iterators\]](string.iterators.md)

```cpp
constexpr explicit basic_string(const Allocator& a) noexcept;
```

**1** *Postconditions:* `size()` is equal to `0`.

```cpp
constexpr basic_string(const basic_string& str);
constexpr basic_string(basic_string&& str) noexcept;
```

**2** *Effects:* Constructs an object whose value is that of `str` prior to this call.

**3** *Remarks:* In the second form, `str` is left in a valid but unspecified state.

```cpp
constexpr basic_string(const basic_string& str, size_type pos,
                       const Allocator& a = Allocator());
constexpr basic_string(const basic_string& str, size_type pos, size_type n,
                       const Allocator& a = Allocator());
```

**4** *Effects:* Let `n` be `npos` for the first overload. Equivalent to:

```cpp
basic_string(basic_string_view<charT, traits>(str).substr(pos, n), a)
```

```cpp
template<class T>
  constexpr basic_string(const T& t, size_type pos, size_type n, const Allocator& a = Allocator());
```

**5** *Constraints:* `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true`.

**6** *Effects:* Creates a variable, `sv`, as if by `basic_string_view<charT, traits> sv = t;` and then behaves the same as:

```cpp
basic_string(sv.substr(pos, n), a);
```

```cpp
template<class T>
  constexpr explicit basic_string(const T& t, const Allocator& a = Allocator());
```

**7** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**8** *Effects:* Creates a variable, `sv`, as if by `basic_string_view<charT, traits> sv = t;` and then behaves the same as `basic_string(sv.data(), sv.size(), a)`.

```cpp
constexpr basic_string(const charT* s, size_type n, const Allocator& a = Allocator());
```

**9** *Preconditions:* `[s, s + n)` is a valid range.

**10** *Effects:* Constructs an object whose initial value is the range `[s, s + n)`.

**11** *Postconditions:* `size()` is equal to `n`, and `traits::compare(data(), s, n)` is equal to `0`.

```cpp
constexpr basic_string(const charT* s, const Allocator& a = Allocator());
```

**12** *Constraints:* `Allocator` is a type that qualifies as an allocator ([[container.requirements.general]](../containers/container.requirements.general.md)). [*Note 1*: This affects class template argument deduction. — *end note*]

**13** *Effects:* Equivalent to: `basic_string(s, traits::length(s), a)`.

```cpp
constexpr basic_string(size_type n, charT c, const Allocator& a = Allocator());
```

**14** *Constraints:* `Allocator` is a type that qualifies as an allocator ([[container.requirements.general]](../containers/container.requirements.general.md)). [*Note 2*: This affects class template argument deduction. — *end note*]

**15** *Effects:* Constructs an object whose value consists of `n` copies of `c`.

```cpp
template<class InputIterator>
  constexpr basic_string(InputIterator begin, InputIterator end, const Allocator& a = Allocator());
```

**16** *Constraints:* `InputIterator` is a type that qualifies as an input iterator ([[container.requirements.general]](../containers/container.requirements.general.md)).

**17** *Effects:* Constructs a string from the values in the range `[begin, end)`, as indicated in [Table 77](../containers/sequence.reqmts.md#tab:container.seq.req).

```cpp
constexpr basic_string(initializer_list<charT> il, const Allocator& a = Allocator());
```

**18** *Effects:* Equivalent to `basic_string(il.begin(), il.end(), a)`.

```cpp
constexpr basic_string(const basic_string& str, const Allocator& alloc);
constexpr basic_string(basic_string&& str, const Allocator& alloc);
```

**19** *Effects:* Constructs an object whose value is that of `str` prior to this call. The stored allocator is constructed from `alloc`. In the second form, `str` is left in a valid but unspecified state.

**20** *Throws:* The second form throws nothing if `alloc == str.get_allocator()`.

```cpp
template<class InputIterator,
         class Allocator = allocator<typename iterator_traits<InputIterator>::value_type>>
  basic_string(InputIterator, InputIterator, Allocator = Allocator())
    -> basic_string<typename iterator_traits<InputIterator>::value_type,
                    char_traits<typename iterator_traits<InputIterator>::value_type>,
                    Allocator>;
```

**21** *Constraints:* `InputIterator` is a type that qualifies as an input iterator, and `Allocator` is a type that qualifies as an allocator ([[container.requirements.general]](../containers/container.requirements.general.md)).

```cpp
template<class charT,
         class traits,
         class Allocator = allocator<charT>>
  explicit basic_string(basic_string_view<charT, traits>, const Allocator& = Allocator())
    -> basic_string<charT, traits, Allocator>;

template<class charT,
         class traits,
         class Allocator = allocator<charT>>
  basic_string(basic_string_view<charT, traits>,
               typename see below::size_type, typename see below::size_type,
               const Allocator& = Allocator())
    -> basic_string<charT, traits, Allocator>;
```

**22** *Constraints:* `Allocator` is a type that qualifies as an allocator ([[container.requirements.general]](../containers/container.requirements.general.md)).

```cpp
constexpr basic_string& operator=(const basic_string& str);
```

**23** *Effects:* If `*this` and `str` are the same object, has no effect. Otherwise, replaces the value of `*this` with a copy of `str`.

**24** *Returns:* `*this`.

```cpp
constexpr basic_string& operator=(basic_string&& str)
  noexcept(allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
           allocator_traits<Allocator>::is_always_equal::value);
```

**25** *Effects:* Move assigns as a sequence container ([[container.requirements]](../containers/container.requirements.md)), except that iterators, pointers and references may be invalidated.

**26** *Returns:* `*this`.

```cpp
template<class T>
  constexpr basic_string& operator=(const T& t);
```

**27** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**28** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return assign(sv);
```

```cpp
constexpr basic_string& operator=(const charT* s);
```

**29** *Effects:* Equivalent to: `return *this = basic_string_view<charT, traits>(s);`

```cpp
constexpr basic_string& operator=(charT c);
```

**30** *Effects:* Equivalent to:

```cpp
return *this = basic_string_view<charT, traits>(addressof(c), 1);
```

```cpp
constexpr basic_string& operator=(initializer_list<charT> il);
```

**31** *Effects:* Equivalent to:

```cpp
return *this = basic_string_view<charT, traits>(il.begin(), il.size());
```
