# 24.6.4.3 Class template `basic_istream_view::iterator` [range.istream.iterator]

[Contents](../../README.md) · up: [24.6.4 Istream view \[range.istream\]](range.istream.md) · prev: [24.6.4.2 Class template `basic_istream_view` \[range.istream.view\]](range.istream.view.md) · next: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md)

```cpp
namespace std::ranges {
  template<movable Val, class CharT, class Traits>
    requires default_initializable<Val> &&
             stream-extractable<Val, CharT, Traits>
  class basic_istream_view<Val, CharT, Traits>::iterator {      // exposition only
  public:
    using iterator_concept = input_iterator_tag;
    using difference_type = ptrdiff_t;
    using value_type = Val;

    iterator() = default;
    constexpr explicit iterator(basic_istream_view& parent) noexcept;

    iterator(const iterator&) = delete;
    iterator(iterator&&) = default;

    iterator& operator=(const iterator&) = delete;
    iterator& operator=(iterator&&) = default;

    iterator& operator++();
    void operator++(int);

    Val& operator*() const;

    friend bool operator==(const iterator& x, default_sentinel_t);

  private:
    basic_istream_view* parent_ = nullptr;                      // exposition only
  };
}
```

```cpp
constexpr explicit iterator(basic_istream_view& parent) noexcept;
```

**1** *Effects:* Initializes `parent_` with `addressof(parent)`.

```cpp
iterator& operator++();
```

**2** *Preconditions:* `parent_->stream_ != nullptr` is `true`.

**3** *Effects:* Equivalent to:

```cpp
*parent_->stream_>> parent_->object_;
return *this;
```

```cpp
void operator++(int);
```

**4** *Preconditions:* `parent_->stream_ != nullptr` is `true`.

**5** *Effects:* Equivalent to `++*this`.

```cpp
Val& operator*() const;
```

**6** *Preconditions:* `parent_->stream_ != nullptr` is `true`.

**7** *Effects:* Equivalent to: `return parent_->object_;`

```cpp
friend bool operator==(const iterator& x, default_sentinel_t);
```

**8** *Effects:* Equivalent to: `return x.parent_ == nullptr || !*x.parent_->stream_;`
