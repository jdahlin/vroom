# 29.7.4.6 Class template `basic_iostream` [iostreamclass]

[Contents](../README.md) · up: [29.7.4 Input streams \[input.streams\]](input.streams.md) · prev: [29.7.4.5 Rvalue stream extraction \[istream.rvalue\]](istream.rvalue.md) · next: [29.7.4.6.1 Constructors \[iostream.cons\]](iostream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_iostream
    : public basic_istream<charT, traits>,
      public basic_ostream<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [iostream.cons], constructor
    explicit basic_iostream(basic_streambuf<charT, traits>* sb);

    // [iostream.dest], destructor
    virtual ~basic_iostream();

  protected:
    // [iostream.cons], constructor
    basic_iostream(const basic_iostream&) = delete;
    basic_iostream(basic_iostream&& rhs);

    // [iostream.assign], assign and swap
    basic_iostream& operator=(const basic_iostream&) = delete;
    basic_iostream& operator=(basic_iostream&& rhs);
    void swap(basic_iostream& rhs);
  };
}
```

**1** The class template `basic_iostream` inherits a number of functions that allow reading input and writing output to sequences controlled by a stream buffer.

## Subclauses

- [29.7.4.6.1 Constructors \[iostream.cons\]](iostream.cons.md)
- [29.7.4.6.2 Destructor \[iostream.dest\]](iostream.dest.md)
- [29.7.4.6.3 Assignment and swap \[iostream.assign\]](iostream.assign.md)
