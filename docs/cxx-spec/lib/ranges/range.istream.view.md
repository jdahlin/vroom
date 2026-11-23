# 24.6.4.2 Class template `basic_istream_view` [range.istream.view]

[Contents](../../README.md) · up: [24.6.4 Istream view \[range.istream\]](range.istream.md) · prev: [24.6.4.1 Overview \[range.istream.overview\]](range.istream.overview.md) · next: [24.6.4.3 Class template `basic_istream_view::iterator` \[range.istream.iterator\]](range.istream.iterator.md)

```cpp
namespace std::ranges {
  template<class Val, class CharT, class Traits>
    concept stream-extractable =                // exposition only
      requires(basic_istream<CharT, Traits>& is, Val& t) {
         is >> t;
      };

  template<movable Val, class CharT, class Traits>
    requires default_initializable<Val> &&
             stream-extractable<Val, CharT, Traits>
  class basic_istream_view : public view_interface<basic_istream_view<Val, CharT, Traits>> {
  public:
    basic_istream_view() = default;
    constexpr explicit basic_istream_view(basic_istream<CharT, Traits>& stream);

    constexpr auto begin()
    {
      if (stream_) {
        *stream_ >> object_;
      }
      return iterator{*this};
    }

    constexpr default_sentinel_t end() const noexcept;

  private:
    struct iterator;                                    // exposition only
    basic_istream<CharT, Traits>* stream_ = nullptr;    // exposition only
    Val object_ = Val();                                // exposition only
  };
}
```

```cpp
constexpr explicit basic_istream_view(basic_istream<CharT, Traits>& stream);
```

**1** *Effects:* Initializes `stream_` with `addressof(stream)`.

```cpp
constexpr default_sentinel_t end() const noexcept;
```

**2** *Effects:* Equivalent to: `return default_sentinel;`

```cpp
template<class Val, class CharT, class Traits>
basic_istream_view<Val, CharT, Traits> istream_view(basic_istream<CharT, Traits>& s);
```

**3** *Effects:* Equivalent to: `return basic_istream_view<Val, CharT, Traits>{s};`
