# 23.6.2 Class template `ostream_iterator` [ostream.iterator]

[Contents](../README.md) · up: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md) · prev: [23.6.1.2 Operations \[istream.iterator.ops\]](istream.iterator.ops.md) · next: [23.6.2.1 Constructors and destructor \[ostream.iterator.cons.des\]](ostream.iterator.cons.des.md)

**1** `ostream_iterator` writes (using `operator<<`) successive elements onto the output stream from which it was constructed. If it was constructed with `charT*` as a constructor argument, this string, called a *delimiter string*, is written to the stream after every `T` is written.

```cpp
namespace std {
  template<class T, class charT = char, class traits = char_traits<charT>>
  class ostream_iterator {
  public:
    using iterator_category = output_iterator_tag;
    using value_type        = void;
    using difference_type   = ptrdiff_t;
    using pointer           = void;
    using reference         = void;
    using char_type         = charT;
    using traits_type       = traits;
    using ostream_type      = basic_ostream<charT,traits>;

    constexpr ostream_iterator() noexcept = default;
    ostream_iterator(ostream_type& s);
    ostream_iterator(ostream_type& s, const charT* delimiter);
    ostream_iterator(const ostream_iterator& x);
    ~ostream_iterator();
    ostream_iterator& operator=(const ostream_iterator&) = default;
    ostream_iterator& operator=(const T& value);

    ostream_iterator& operator*();
    ostream_iterator& operator++();
    ostream_iterator& operator++(int);

  private:
    basic_ostream<charT,traits>* out_stream = nullptr;          // exposition only
    const charT* delim = nullptr;                               // exposition only
  };
}
```

## Subclauses

- [23.6.2.1 Constructors and destructor \[ostream.iterator.cons.des\]](ostream.iterator.cons.des.md)
- [23.6.2.2 Operations \[ostream.iterator.ops\]](ostream.iterator.ops.md)
