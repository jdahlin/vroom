# 29.10.3.2 Construction and destruction [syncstream.osyncstream.cons]

[Contents](../../README.md) · up: [29.10.3 Class template `basic_osyncstream` \[syncstream.osyncstream\]](syncstream.osyncstream.md) · prev: [29.10.3.1 Overview \[syncstream.osyncstream.overview\]](syncstream.osyncstream.overview.md) · next: [29.10.3.3 Member functions \[syncstream.osyncstream.members\]](syncstream.osyncstream.members.md)

```cpp
basic_osyncstream(streambuf_type* buf, const Allocator& allocator);
```

**1** *Effects:* Initializes `sb` from `buf` and `allocator`. Initializes the base class with `basic_ostream<charT, traits>(addressof(sb))`.

**2** [*Note 1*: The member functions of the provided stream buffer might be called from `emit()` while a lock is held. Care should be taken to ensure that this does not result in deadlock. — *end note*]

**3** *Postconditions:* `get_wrapped() == buf` is `true`.

```cpp
basic_osyncstream(basic_osyncstream&& other) noexcept;
```

**4** *Effects:* Move constructs the base class and `sb` from the corresponding subobjects of `other`, and calls `basic_ostream<charT, traits>::set_rdbuf(addressof(sb))`.

**5** *Postconditions:* The value returned by `get_wrapped()` is the value returned by `os.get_wrapped()` prior to calling this constructor. `nullptr == other.get_wrapped()` is `true`.
