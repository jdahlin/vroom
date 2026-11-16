# 29.8.4 Class template `basic_ostringstream` [ostringstream]

[Contents](../../README.md) · up: [29.8 String-based streams \[string.streams\]](string.streams.md) · prev: [29.8.3.3 Member functions \[istringstream.members\]](istringstream.members.md) · next: [29.8.4.1 Constructors \[ostringstream.cons\]](ostringstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_ostringstream : public basic_ostream<charT, traits> {
  public:
    using char_type      = charT;
    using int_type       = typename traits::int_type;
    using pos_type       = typename traits::pos_type;
    using off_type       = typename traits::off_type;
    using traits_type    = traits;
    using allocator_type = Allocator;

    // [ostringstream.cons], constructors
    basic_ostringstream() : basic_ostringstream(ios_base::out) {}
    explicit basic_ostringstream(ios_base::openmode which);
    explicit basic_ostringstream(
      const basic_string<charT, traits, Allocator>& s,
      ios_base::openmode which = ios_base::out);
    basic_ostringstream(ios_base::openmode which, const Allocator& a);
    explicit basic_ostringstream(
      basic_string<charT, traits, Allocator>&& s,
      ios_base::openmode which = ios_base::out);
    template<class SAlloc>
      basic_ostringstream(
        const basic_string<charT, traits, SAlloc>& s, const Allocator& a)
        : basic_ostringstream(s, ios_base::out, a) {}
    template<class SAlloc>
      basic_ostringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which, const Allocator& a);
    template<class SAlloc>
      explicit basic_ostringstream(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which = ios_base::out);
    basic_ostringstream(const basic_ostringstream&) = delete;
    basic_ostringstream(basic_ostringstream&& rhs);

    // [ostringstream.assign], assign and swap
    basic_ostringstream& operator=(const basic_ostringstream&) = delete;
    basic_ostringstream& operator=(basic_ostringstream&& rhs);
    void swap(basic_ostringstream& rhs);

    // [ostringstream.members], members
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
    void swap(basic_ostringstream<charT, traits, Allocator>& x,
              basic_ostringstream<charT, traits, Allocator>& y);
}
```

**1** The class `basic_ostringstream<charT, traits, Allocator>` supports writing objects of class `basic_string<charT, traits, Allocator>`. It uses a `basic_stringbuf` object to control the associated storage. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `stringbuf` object.

## Subclauses

- [29.8.4.1 Constructors \[ostringstream.cons\]](ostringstream.cons.md)
- [29.8.4.2 Assignment and swap \[ostringstream.assign\]](ostringstream.assign.md)
- [29.8.4.3 Member functions \[ostringstream.members\]](ostringstream.members.md)
