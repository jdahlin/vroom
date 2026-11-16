# 29.10.3.1 Overview [syncstream.osyncstream.overview]

[Contents](../README.md) · up: [29.10.3 Class template `basic_osyncstream` \[syncstream.osyncstream\]](syncstream.osyncstream.md) · prev: [29.10.3 Class template `basic_osyncstream` \[syncstream.osyncstream\]](syncstream.osyncstream.md) · next: [29.10.3.2 Construction and destruction \[syncstream.osyncstream.cons\]](syncstream.osyncstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>, class Allocator = allocator<charT>>
  class basic_osyncstream : public basic_ostream<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    using allocator_type = Allocator;
    using streambuf_type = basic_streambuf<charT, traits>;
    using syncbuf_type   = basic_syncbuf<charT, traits, Allocator>;

    // [syncstream.osyncstream.cons], construction and destruction
    basic_osyncstream(streambuf_type*, const Allocator&);
    explicit basic_osyncstream(streambuf_type* obuf)
      : basic_osyncstream(obuf, Allocator()) {}
    basic_osyncstream(basic_ostream<charT, traits>& os, const Allocator& allocator)
      : basic_osyncstream(os.rdbuf(), allocator) {}
    explicit basic_osyncstream(basic_ostream<charT, traits>& os)
      : basic_osyncstream(os, Allocator()) {}
    basic_osyncstream(basic_osyncstream&&) noexcept;
    ~basic_osyncstream();

    // assignment
    basic_osyncstream& operator=(basic_osyncstream&&) noexcept;

    // [syncstream.osyncstream.members], member functions
    void emit();
    streambuf_type* get_wrapped() const noexcept;
    syncbuf_type* rdbuf() const noexcept { return const_cast<syncbuf_type*>(addressof(sb)); }

  private:
    syncbuf_type sb;    // exposition only
  };
}
```

**1** `Allocator` shall meet the *Cpp17Allocator* requirements ([Table 36](allocator.requirements.md#tab:cpp17.allocator)).

**2** [*Example 1*: A named variable can be used within a block statement for streaming.

```cpp
{
  osyncstream bout(cout);
  bout << "Hello, ";
  bout << "World!";
  bout << endl; // flush is noted
  bout << "and more!\n";
}   // characters are transferred and cout is flushed
```

— *end example*]

**3** [*Example 2*: A temporary object can be used for streaming within a single statement.

```cpp
osyncstream(cout) << "Hello, " << "World!" << '\n';
```

In this example, `cout` is not flushed. — *end example*]
