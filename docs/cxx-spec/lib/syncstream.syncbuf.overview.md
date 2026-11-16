# 29.10.2.1 Overview [syncstream.syncbuf.overview]

[Contents](../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · next: [29.10.2.2 Construction and destruction \[syncstream.syncbuf.cons\]](syncstream.syncbuf.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>, class Allocator = allocator<charT>>
  class basic_syncbuf : public basic_streambuf<charT, traits> {
  public:
    using char_type      = charT;
    using int_type       = typename traits::int_type;
    using pos_type       = typename traits::pos_type;
    using off_type       = typename traits::off_type;
    using traits_type    = traits;
    using allocator_type = Allocator;

    using streambuf_type = basic_streambuf<charT, traits>;

    // [syncstream.syncbuf.cons], construction and destruction
    basic_syncbuf()
      : basic_syncbuf(nullptr) {}
    explicit basic_syncbuf(streambuf_type* obuf)
      : basic_syncbuf(obuf, Allocator()) {}
    basic_syncbuf(streambuf_type*, const Allocator&);
    basic_syncbuf(basic_syncbuf&&);
    ~basic_syncbuf();

    // [syncstream.syncbuf.assign], assignment and swap
    basic_syncbuf& operator=(basic_syncbuf&&);
    void swap(basic_syncbuf&);

    // [syncstream.syncbuf.members], member functions
    bool emit();
    streambuf_type* get_wrapped() const noexcept;
    allocator_type get_allocator() const noexcept;
    void set_emit_on_sync(bool) noexcept;

  protected:
    // [syncstream.syncbuf.virtuals], overridden virtual functions
    int sync() override;

  private:
    streambuf_type* wrapped;    // exposition only
    bool emit_on_sync{};        // exposition only
  };

  // [syncstream.syncbuf.special], specialized algorithms
  template<class charT, class traits, class Allocator>
    void swap(basic_syncbuf<charT, traits, Allocator>&,
              basic_syncbuf<charT, traits, Allocator>&);
}
```

**1** Class template `basic_syncbuf` stores character data written to it, known as the associated output, into internal buffers allocated using the object’s allocator. The associated output is transferred to the wrapped stream buffer object `*wrapped` when `emit()` is called or when the `basic_syncbuf` object is destroyed. Such transfers are atomic with respect to transfers by other `basic_syncbuf` objects with the same wrapped stream buffer object.
