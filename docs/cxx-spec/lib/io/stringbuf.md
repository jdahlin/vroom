# 29.8.2 Class template `basic_stringbuf` [stringbuf]

[Contents](../../README.md) · up: [29.8 String-based streams \[string.streams\]](string.streams.md) · prev: [29.8.1 Header `<sstream>` synopsis \[sstream.syn\]](sstream.syn.md) · next: [29.8.2.1 Constructors \[stringbuf.cons\]](stringbuf.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_stringbuf : public basic_streambuf<charT, traits> {
  public:
    using char_type      = charT;
    using int_type       = typename traits::int_type;
    using pos_type       = typename traits::pos_type;
    using off_type       = typename traits::off_type;
    using traits_type    = traits;
    using allocator_type = Allocator;

    // [stringbuf.cons], constructors
    basic_stringbuf() : basic_stringbuf(ios_base::in | ios_base::out) {}
    explicit basic_stringbuf(ios_base::openmode which);
    explicit basic_stringbuf(
      const basic_string<charT, traits, Allocator>& s,
      ios_base::openmode which = ios_base::in | ios_base::out);
    explicit basic_stringbuf(const Allocator& a)
      : basic_stringbuf(ios_base::in | ios_base::out, a) {}
    basic_stringbuf(ios_base::openmode which, const Allocator& a);
    explicit basic_stringbuf(
      basic_string<charT, traits, Allocator>&& s,
      ios_base::openmode which = ios_base::in | ios_base::out);
    template<class SAlloc>
      basic_stringbuf(
        const basic_string<charT, traits, SAlloc>& s, const Allocator& a)
        : basic_stringbuf(s, ios_base::in | ios_base::out, a) {}
    template<class SAlloc>
      basic_stringbuf(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which, const Allocator& a);
    template<class SAlloc>
      explicit basic_stringbuf(
        const basic_string<charT, traits, SAlloc>& s,
        ios_base::openmode which = ios_base::in | ios_base::out);
    basic_stringbuf(const basic_stringbuf&) = delete;
    basic_stringbuf(basic_stringbuf&& rhs);
    basic_stringbuf(basic_stringbuf&& rhs, const Allocator& a);

    // [stringbuf.assign], assign and swap
    basic_stringbuf& operator=(const basic_stringbuf&) = delete;
    basic_stringbuf& operator=(basic_stringbuf&& rhs);
    void swap(basic_stringbuf& rhs) noexcept(see below);

    // [stringbuf.members], getters and setters
    allocator_type get_allocator() const noexcept;

    basic_string<charT, traits, Allocator> str() const &;
    template<class SAlloc>
      basic_string<charT,traits,SAlloc> str(const SAlloc& sa) const;
    basic_string<charT, traits, Allocator> str() &&;
    basic_string_view<charT, traits> view() const noexcept;

    void str(const basic_string<charT, traits, Allocator>& s);
    template<class SAlloc>
      void str(const basic_string<charT, traits, SAlloc>& s);
    void str(basic_string<charT, traits, Allocator>&& s);

  protected:
    // [stringbuf.virtuals], overridden virtual functions
    int_type underflow() override;
    int_type pbackfail(int_type c = traits::eof()) override;
    int_type overflow (int_type c = traits::eof()) override;
    basic_streambuf<charT, traits>* setbuf(charT*, streamsize) override;

    pos_type seekoff(off_type off, ios_base::seekdir way,
                     ios_base::openmode which
                      = ios_base::in | ios_base::out) override;
    pos_type seekpos(pos_type sp,
                     ios_base::openmode which
                      = ios_base::in | ios_base::out) override;

  private:
    ios_base::openmode mode;                        // exposition only
    basic_string<charT, traits, Allocator> buf;     // exposition only
    void init_buf_ptrs();                           // exposition only
  };

  template<class charT, class traits, class Allocator>
    void swap(basic_stringbuf<charT, traits, Allocator>& x,
              basic_stringbuf<charT, traits, Allocator>& y) noexcept(noexcept(x.swap(y)));
}
```

**1** The class `basic_stringbuf` is derived from `basic_streambuf` to associate possibly the input sequence and possibly the output sequence with a sequence of arbitrary *characters*. The sequence can be initialized from, or made available as, an object of class `basic_string`.

**2** For the sake of exposition, the maintained data and internal pointer initialization is presented here as:

- `ios_base::openmode mode`, has `in` set if the input sequence can be read, and `out` set if the output sequence can be written.
- `basic_string<charT, traits, Allocator> buf` contains the underlying character sequence.
- `init_buf_ptrs()` sets the base class’ get area ([[streambuf.get.area]](streambuf.get.area.md)) and put area ([[streambuf.put.area]](streambuf.put.area.md)) pointers after initializing, moving from, or assigning to `buf` accordingly.

## Subclauses

- [29.8.2.1 Constructors \[stringbuf.cons\]](stringbuf.cons.md)
- [29.8.2.2 Assignment and swap \[stringbuf.assign\]](stringbuf.assign.md)
- [29.8.2.3 Member functions \[stringbuf.members\]](stringbuf.members.md)
- [29.8.2.4 Overridden virtual functions \[stringbuf.virtuals\]](stringbuf.virtuals.md)
