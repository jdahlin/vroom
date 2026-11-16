# 23.6.4 Class template `ostreambuf_iterator` [ostreambuf.iterator]

[Contents](../README.md) · up: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md) · prev: [23.6.3.3 Operations \[istreambuf.iterator.ops\]](istreambuf.iterator.ops.md) · next: [23.6.4.1 Constructors \[ostreambuf.iter.cons\]](ostreambuf.iter.cons.md)

**1** The class template `ostreambuf_iterator` writes successive *characters* onto the output stream from which it was constructed.

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class ostreambuf_iterator {
  public:
    using iterator_category = output_iterator_tag;
    using value_type        = void;
    using difference_type   = ptrdiff_t;
    using pointer           = void;
    using reference         = void;
    using char_type         = charT;
    using traits_type       = traits;
    using streambuf_type    = basic_streambuf<charT,traits>;
    using ostream_type      = basic_ostream<charT,traits>;

    constexpr ostreambuf_iterator() noexcept = default;
    ostreambuf_iterator(ostream_type& s) noexcept;
    ostreambuf_iterator(streambuf_type* s) noexcept;
    ostreambuf_iterator& operator=(charT c);

    ostreambuf_iterator& operator*();
    ostreambuf_iterator& operator++();
    ostreambuf_iterator& operator++(int);
    bool failed() const noexcept;

  private:
    streambuf_type* sbuf_ = nullptr;    // exposition only
  };
}
```

## Subclauses

- [23.6.4.1 Constructors \[ostreambuf.iter.cons\]](ostreambuf.iter.cons.md)
- [23.6.4.2 Operations \[ostreambuf.iter.ops\]](ostreambuf.iter.ops.md)
