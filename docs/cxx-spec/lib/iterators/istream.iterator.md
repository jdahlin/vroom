# 23.6.1 Class template `istream_iterator` [istream.iterator]

[Contents](../../README.md) · up: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md) · prev: [23.6 Stream iterators \[stream.iterators\]](stream.iterators.md) · next: [23.6.1.1 Constructors and destructor \[istream.iterator.cons\]](istream.iterator.cons.md)

**1** The class template `istream_iterator` is an input iterator ([[input.iterators]](input.iterators.md)) that reads successive elements from the input stream for which it was constructed.

```cpp
namespace std {
  template<class T, class charT = char, class traits = char_traits<charT>,
           class Distance = ptrdiff_t>
  class istream_iterator {
  public:
    using iterator_category = input_iterator_tag;
    using value_type        = T;
    using difference_type   = Distance;
    using pointer           = const T*;
    using reference         = const T&;
    using char_type         = charT;
    using traits_type       = traits;
    using istream_type      = basic_istream<charT,traits>;

    constexpr istream_iterator();
    constexpr istream_iterator(default_sentinel_t);
    istream_iterator(istream_type& s);
    istream_iterator(const istream_iterator& x) = default;
    ~istream_iterator() = default;
    istream_iterator& operator=(const istream_iterator&) = default;

    const T& operator*() const;
    const T* operator->() const;
    istream_iterator& operator++();
    istream_iterator  operator++(int);

    friend bool operator==(const istream_iterator& i, default_sentinel_t);

  private:
    basic_istream<charT,traits>* in_stream; // exposition only
    T value;                                // exposition only
  };
}
```

**2** The type `T` shall meet the *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, and *Cpp17CopyAssignable* requirements.

## Subclauses

- [23.6.1.1 Constructors and destructor \[istream.iterator.cons\]](istream.iterator.cons.md)
- [23.6.1.2 Operations \[istream.iterator.ops\]](istream.iterator.ops.md)
