# 23.6.3 Class template `istreambuf_iterator` [istreambuf.iterator]

[Contents](../../README.md) · up: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md) · prev: [23.6.2.2 Operations \[ostream.iterator.ops\]](ostream.iterator.ops.md) · next: [23.6.3.1 Class `istreambuf_iterator::proxy` \[istreambuf.iterator.proxy\]](istreambuf.iterator.proxy.md)

**1** The class template `istreambuf_iterator` defines an input iterator ([[input.iterators]](input.iterators.md)) that reads successive *characters* from the streambuf for which it was constructed. `operator*` provides access to the current input character, if any. Each time `operator++` is evaluated, the iterator advances to the next input character. If the end of stream is reached (`streambuf_type::sgetc()` returns `traits::eof()`), the iterator becomes equal to the *end-of-stream* iterator value. The default constructor `istreambuf_iterator()` and the constructor `istreambuf_iterator(nullptr)` both construct an end-of-stream iterator object suitable for use as an end-of-range. All specializations of `istreambuf_iterator` shall have a trivial copy constructor, a `constexpr` default constructor, and a trivial destructor.

**2** The result of `operator*()` on an end-of-stream iterator is undefined. For any other iterator value a `char_type` value is returned. It is impossible to assign a character via an input iterator.

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class istreambuf_iterator {
  public:
    using iterator_category = input_iterator_tag;
    using value_type        = charT;
    using difference_type   = typename traits::off_type;
    using pointer           = unspecified;
    using reference         = charT;
    using char_type         = charT;
    using traits_type       = traits;
    using int_type          = typename traits::int_type;
    using streambuf_type    = basic_streambuf<charT,traits>;
    using istream_type      = basic_istream<charT,traits>;

    class proxy;                          // exposition only

    constexpr istreambuf_iterator() noexcept;
    constexpr istreambuf_iterator(default_sentinel_t) noexcept;
    istreambuf_iterator(const istreambuf_iterator&) noexcept = default;
    ~istreambuf_iterator() = default;
    istreambuf_iterator(istream_type& s) noexcept;
    istreambuf_iterator(streambuf_type* s) noexcept;
    istreambuf_iterator(const proxy& p) noexcept;
    istreambuf_iterator& operator=(const istreambuf_iterator&) noexcept = default;
    charT operator*() const;
    istreambuf_iterator& operator++();
    proxy operator++(int);
    bool equal(const istreambuf_iterator& b) const;

    friend bool operator==(const istreambuf_iterator& i, default_sentinel_t s);

  private:
    streambuf_type* sbuf_;              // exposition only
  };
}
```

## Subclauses

- [23.6.3.1 Class `istreambuf_iterator::proxy` \[istreambuf.iterator.proxy\]](istreambuf.iterator.proxy.md)
- [23.6.3.2 Constructors \[istreambuf.iterator.cons\]](istreambuf.iterator.cons.md)
- [23.6.3.3 Operations \[istreambuf.iterator.ops\]](istreambuf.iterator.ops.md)
