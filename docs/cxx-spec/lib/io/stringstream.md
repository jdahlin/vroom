# 29.8.5 Class template `basic_stringstream` [stringstream]

[Contents](../../README.md) · up: [29.8 String-based streams \[string.streams\]](string.streams.md) · prev: [29.8.4.3 Member functions \[ostringstream.members\]](ostringstream.members.md) · next: [29.8.5.1 Constructors \[stringstream.cons\]](stringstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_stringstream : public basic_iostream<charT, traits> {
  public:
    using char_type      = charT;
    using int_type       = typename traits::int_type;
    using pos_type       = typename traits::pos_type;
    using off_type       = typename traits::off_type;
    using traits_type    = traits;
    using allocator_type = Allocator;

    // [stringstream.cons], constructors
    basic_stringstream() : basic_stringstream(ios_base::out | ios_base::in) {}
    explicit basic_stringstream(ios_base::openmode which);
    explicit basic_stringstream(
      const basic_string<charT, traits, Allocator>& s,
      ios_base::openmode which = ios_base::out | ios_base::in);
    basic_stringstream(ios_base::openmode which, const Allocator& a);
    explicit basic_stringstream(
      basic_string<charT, traits, Allocator>&& s,
      ios_base::openmode which = ios_base::out | ios_base::in);
    template<class SAlloc>
      basic_stringstream(
        const basic_string<charT, traits, SAlloc>& s, const Allocator& a)
        : basic_stringstream(s, ios_base::out | ios_base::in, a) {}
    template<class SAlloc>
      basic_stringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which, const Allocator& a);
    template<class SAlloc>
      explicit basic_stringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which = ios_base::out | ios_base::in);
    basic_stringstream(const basic_stringstream&) = delete;
    basic_stringstream(basic_stringstream&& rhs);

    // [stringstream.assign], assign and swap
    basic_stringstream& operator=(const basic_stringstream&) = delete;
    basic_stringstream& operator=(basic_stringstream&& rhs);
    void swap(basic_stringstream& rhs);

    // [stringstream.members], members
    basic_stringbuf<charT, traits, Allocator>* rdbuf() const;

    basic_string<charT, traits, Allocator> str() const &;
    template<class SAlloc>
      basic_string<charT,traits,SAlloc> str(const SAlloc& sa) const;
    basic_string<charT, traits, Allocator> str() &&;
    basic_string_view<charT, traits> view() const noexcept;

    void str(const basic_string<charT, traits, Allocator>& s);
    template<class SAlloc>
      void str(const basic_string<charT, traits, SAlloc>& s);
    void str(basic_string<charT, traits, Allocator>&& s);

  private:
    basic_stringbuf<charT, traits> sb;  // exposition only
  };

  template<class charT, class traits, class Allocator>
    void swap(basic_stringstream<charT, traits, Allocator>& x,
              basic_stringstream<charT, traits, Allocator>& y);
}
```

**1** The class template `basic_stringstream<charT, traits>` supports reading and writing from objects of class `basic_string<charT, traits, Allocator>`. It uses a `basic_stringbuf<charT, traits, Allocator>` object to control the associated sequence. For the sake of exposition, the maintained data is presented here as

- `sb`, the `stringbuf` object.

## Subclauses

- [29.8.5.1 Constructors \[stringstream.cons\]](stringstream.cons.md)
- [29.8.5.2 Assignment and swap \[stringstream.assign\]](stringstream.assign.md)
- [29.8.5.3 Member functions \[stringstream.members\]](stringstream.members.md)
