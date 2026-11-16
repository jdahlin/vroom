# 29.6.3 Class template `basic_streambuf` [streambuf]

[Contents](../../README.md) · up: [29.6 Stream buffers \[stream.buffers\]](stream.buffers.md) · prev: [29.6.2 Stream buffer requirements \[streambuf.reqts\]](streambuf.reqts.md) · next: [29.6.3.1 Constructors \[streambuf.cons\]](streambuf.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_streambuf {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    virtual ~basic_streambuf();

    // [streambuf.locales], locales
    locale   pubimbue(const locale& loc);
    locale   getloc() const;

    // [streambuf.buffer], buffer and positioning
    basic_streambuf* pubsetbuf(char_type* s, streamsize n);
    pos_type pubseekoff(off_type off, ios_base::seekdir way,
                        ios_base::openmode which
                          = ios_base::in | ios_base::out);
    pos_type pubseekpos(pos_type sp,
                        ios_base::openmode which
                          = ios_base::in | ios_base::out);
    int      pubsync();

    // get and put areas
    // [streambuf.pub.get], get area
    streamsize in_avail();
    int_type snextc();
    int_type sbumpc();
    int_type sgetc();
    streamsize sgetn(char_type* s, streamsize n);

    // [streambuf.pub.pback], putback
    int_type sputbackc(char_type c);
    int_type sungetc();

    // [streambuf.pub.put], put area
    int_type   sputc(char_type c);
    streamsize sputn(const char_type* s, streamsize n);

  protected:
    basic_streambuf();
    basic_streambuf(const basic_streambuf& rhs);
    basic_streambuf& operator=(const basic_streambuf& rhs);

    void swap(basic_streambuf& rhs);

    // [streambuf.get.area], get area access
    char_type* eback() const;
    char_type* gptr()  const;
    char_type* egptr() const;
    void       gbump(int n);
    void       setg(char_type* gbeg, char_type* gnext, char_type* gend);

    // [streambuf.put.area], put area access
    char_type* pbase() const;
    char_type* pptr() const;
    char_type* epptr() const;
    void       pbump(int n);
    void       setp(char_type* pbeg, char_type* pend);

    // [streambuf.virtuals], virtual functions
    // [streambuf.virt.locales], locales
    virtual void imbue(const locale& loc);

    // [streambuf.virt.buffer], buffer management and positioning
    virtual basic_streambuf* setbuf(char_type* s, streamsize n);
    virtual pos_type seekoff(off_type off, ios_base::seekdir way,
                             ios_base::openmode which
                               = ios_base::in | ios_base::out);
    virtual pos_type seekpos(pos_type sp,
                             ios_base::openmode which
                               = ios_base::in | ios_base::out);
    virtual int      sync();

    // [streambuf.virt.get], get area
    virtual streamsize showmanyc();
    virtual streamsize xsgetn(char_type* s, streamsize n);
    virtual int_type   underflow();
    virtual int_type   uflow();

    // [streambuf.virt.pback], putback
    virtual int_type   pbackfail(int_type c = traits::eof());

    // [streambuf.virt.put], put area
    virtual streamsize xsputn(const char_type* s, streamsize n);
    virtual int_type   overflow(int_type c = traits::eof());
  };
}
```

**1** The class template `basic_streambuf` serves as an abstract base class for deriving various *stream buffers* whose objects each control two *character sequences*:

- a character *input sequence*;
- a character *output sequence*.

## Subclauses

- [29.6.3.1 Constructors \[streambuf.cons\]](streambuf.cons.md)
- [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md)
  - [29.6.3.2.1 Locales \[streambuf.locales\]](streambuf.locales.md)
  - [29.6.3.2.2 Buffer management and positioning \[streambuf.buffer\]](streambuf.buffer.md)
  - [29.6.3.2.3 Get area \[streambuf.pub.get\]](streambuf.pub.get.md)
  - [29.6.3.2.4 Putback \[streambuf.pub.pback\]](streambuf.pub.pback.md)
  - [29.6.3.2.5 Put area \[streambuf.pub.put\]](streambuf.pub.put.md)
- [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md)
  - [29.6.3.3.1 Assignment \[streambuf.assign\]](streambuf.assign.md)
  - [29.6.3.3.2 Get area access \[streambuf.get.area\]](streambuf.get.area.md)
  - [29.6.3.3.3 Put area access \[streambuf.put.area\]](streambuf.put.area.md)
- [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md)
  - [29.6.3.4.1 Locales \[streambuf.virt.locales\]](streambuf.virt.locales.md)
  - [29.6.3.4.2 Buffer management and positioning \[streambuf.virt.buffer\]](streambuf.virt.buffer.md)
  - [29.6.3.4.3 Get area \[streambuf.virt.get\]](streambuf.virt.get.md)
  - [29.6.3.4.4 Putback \[streambuf.virt.pback\]](streambuf.virt.pback.md)
  - [29.6.3.4.5 Put area \[streambuf.virt.put\]](streambuf.virt.put.md)
