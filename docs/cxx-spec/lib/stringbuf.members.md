# 29.8.2.3 Member functions [stringbuf.members]

[Contents](../README.md) · up: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md) · prev: [29.8.2.2 Assignment and swap \[stringbuf.assign\]](stringbuf.assign.md) · next: [29.8.2.4 Overridden virtual functions \[stringbuf.virtuals\]](stringbuf.virtuals.md)

**1** The member functions getting the underlying character sequence all refer to a `high_mark` value, where `high_mark` represents the position one past the highest initialized character in the buffer. Characters can be initialized by writing to the stream, by constructing the `basic_stringbuf` passing a `basic_string` argument, or by calling one of the `str` member functions passing a `basic_string` as an argument. In the latter case, all characters initialized prior to the call are now considered uninitialized (except for those characters re-initialized by the new `basic_string`).

```cpp
void init_buf_ptrs(); // exposition only
```

**2** *Effects:* Initializes the input and output sequences from `buf` according to `mode`.

**3** *Postconditions:*

- If `ios_base::out` is set in `mode`, `pbase()` points to `buf.front()` and `epptr() >= pbase() + buf.size()` is `true`;

  - in addition, if `ios_base::ate` is set in `mode`, `pptr() == pbase() + buf.size()` is `true`,
  - otherwise `pptr() == pbase()` is `true`.

- If `ios_base::in` is set in `mode`, `eback()` points to `buf.front()`, and `(gptr() == eback() && egptr() == eback() + buf.size())` is `true`.

**4** [*Note 1*: For efficiency reasons, stream buffer operations might violate invariants of `buf` while it is held encapsulated in the `basic_stringbuf`, e.g., by writing to characters in the range `[buf.data() + buf.size(), buf.data() + buf.capacity())`. All operations retrieving a `basic_string` from `buf` ensure that the `basic_string` invariants hold on the returned value. — *end note*]

```cpp
allocator_type get_allocator() const noexcept;
```

**5** *Returns:* `buf.get_allocator()`.

```cpp
basic_string<charT, traits, Allocator> str() const &;
```

**6** *Effects:* Equivalent to:

```cpp
return basic_string<charT, traits, Allocator>(view(), get_allocator());
```

```cpp
template<class SAlloc>
  basic_string<charT, traits, SAlloc> str(const SAlloc& sa) const;
```

**7** *Constraints:* `SAlloc` is a type that qualifies as an allocator ([[container.requirements.general]](container.requirements.general.md)).

**8** *Effects:* Equivalent to:

```cpp
return basic_string<charT, traits, SAlloc>(view(), sa);
```

```cpp
basic_string<charT, traits, Allocator> str() &&;
```

**9** *Returns:* A `basic_string<charT, traits, Allocator>` object move constructed from the `basic_stringbuf`’s underlying character sequence in `buf`. This can be achieved by first adjusting `buf` to have the same content as `view()`.

**10** *Postconditions:* The underlying character sequence `buf` is empty and `pbase()`, `pptr()`, `epptr()`, `eback()`, `gptr()`, and `egptr()` are initialized as if by calling `init_buf_ptrs()` with an empty `buf`.

```cpp
basic_string_view<charT, traits> view() const noexcept;
```

**11** Let `sv` be `basic_string_view<charT, traits>`.

**12** *Returns:* A `sv` object referring to the `basic_stringbuf`’s underlying character sequence in `buf`:

- If `ios_base::out` is set in `mode`, then `sv(pbase(), high_mark-pbase())` is returned.
- Otherwise, if `ios_base::in` is set in `mode`, then `sv(eback(), egptr()-eback())` is returned.
- Otherwise, `sv()` is returned.

**13** [*Note 2*: Using the returned `sv` object after destruction or invalidation of the character sequence underlying `*this` is undefined behavior, unless `sv.empty()` is `true`. — *end note*]

```cpp
void str(const basic_string<charT, traits, Allocator>& s);
```

**14** *Effects:* Equivalent to:

```cpp
buf = s;
init_buf_ptrs();
```

```cpp
template<class SAlloc>
  void str(const basic_string<charT, traits, SAlloc>& s);
```

**15** *Constraints:* `is_same_v<SAlloc,Allocator>` is `false`.

**16** *Effects:* Equivalent to:

```cpp
buf = s;
init_buf_ptrs();
```

```cpp
void str(basic_string<charT, traits, Allocator>&& s);
```

**17** *Effects:* Equivalent to:

```cpp
buf = std::move(s);
init_buf_ptrs();
```
