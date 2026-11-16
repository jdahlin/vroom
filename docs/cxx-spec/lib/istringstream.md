# 29.8.3 Class template `basic_istringstream` [istringstream]

[Contents](../README.md) · up: [29.8 String-based streams \[string.streams\]](string.streams.md) · prev: [29.8.2.4 Overridden virtual functions \[stringbuf.virtuals\]](stringbuf.virtuals.md) · next: [29.8.3.1 Constructors \[istringstream.cons\]](istringstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_istringstream : public basic_istream<charT, traits> {
  public:
    using char_type      = charT;
    using int_type       = typename traits::int_type;
    using pos_type       = typename traits::pos_type;
    using off_type       = typename traits::off_type;
    using traits_type    = traits;
    using allocator_type = Allocator;

    // [istringstream.cons], constructors
    basic_istringstream() : basic_istringstream(ios_base::in) {}
    explicit basic_istringstream(ios_base::openmode which);
    explicit basic_istringstream(
      const basic_string<charT, traits, Allocator>& s,
      ios_base::openmode which = ios_base::in);
    basic_istringstream(ios_base::openmode which, const Allocator& a);
    explicit basic_istringstream(
      basic_string<charT, traits, Allocator>&& s,
      ios_base::openmode which = ios_base::in);
    template<class SAlloc>
      basic_istringstream(
        const basic_string<charT, traits, SAlloc>& s, const Allocator& a)
        : basic_istringstream(s, ios_base::in, a) {}
    template<class SAlloc>
      basic_istringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which, const Allocator& a);
    template<class SAlloc>
      explicit basic_istringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which = ios_base::in);
    basic_istringstream(const basic_istringstream&) = delete;
    basic_istringstream(basic_istringstream&& rhs);

    // [istringstream.assign], assign and swap
    basic_istringstream& operator=(const basic_istringstream&) = delete;
    basic_istringstream& operator=(basic_istringstream&& rhs);
    void swap(basic_istringstream& rhs);

    // [istringstream.members], members
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
    basic_stringbuf<charT, traits, Allocator> sb;   // exposition only
  };

  template<class charT, class traits, class Allocator>
    void swap(basic_istringstream<charT, traits, Allocator>& x,
              basic_istringstream<charT, traits, Allocator>& y);
}
```

**1** The class `basic_istringstream<charT, traits, Allocator>` supports reading objects of class `basic_string<charT, traits, Allocator>`. It uses a `basic_stringbuf<charT, traits, Allocator>` object to control the associated storage. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `stringbuf` object.

## Subclauses

- [29.8.3.1 Constructors \[istringstream.cons\]](istringstream.cons.md)
- [29.8.3.2 Assignment and swap \[istringstream.assign\]](istringstream.assign.md)
- [29.8.3.3 Member functions \[istringstream.members\]](istringstream.members.md)
