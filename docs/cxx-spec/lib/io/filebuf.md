# 29.9.2 Class template `basic_filebuf` [filebuf]

[Contents](../../README.md) · up: [29.9 File-based streams \[file.streams\]](file.streams.md) · prev: [29.9.1 Header `<fstream>` synopsis \[fstream.syn\]](fstream.syn.md) · next: [29.9.2.1 Constructors \[filebuf.cons\]](filebuf.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_filebuf : public basic_streambuf<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [filebuf.cons], constructors/destructor
    basic_filebuf();
    basic_filebuf(const basic_filebuf&) = delete;
    basic_filebuf(basic_filebuf&& rhs);
    virtual ~basic_filebuf();

    // [filebuf.assign], assign and swap
    basic_filebuf& operator=(const basic_filebuf&) = delete;
    basic_filebuf& operator=(basic_filebuf&& rhs);
    void swap(basic_filebuf& rhs);

    // [filebuf.members], members
    bool is_open() const;
    basic_filebuf* open(const char* s, ios_base::openmode mode);
    basic_filebuf* open(const filesystem::path::value_type* s,
                        ios_base::openmode mode);   // wide systems only; see [fstream.syn]
    basic_filebuf* open(const string& s,
                        ios_base::openmode mode);
    basic_filebuf* open(const filesystem::path& s,
                        ios_base::openmode mode);
    basic_filebuf* close();

  protected:
    // [filebuf.virtuals], overridden virtual functions
    streamsize showmanyc() override;
    int_type underflow() override;
    int_type uflow() override;
    int_type pbackfail(int_type c = traits::eof()) override;
    int_type overflow (int_type c = traits::eof()) override;

    basic_streambuf<charT, traits>* setbuf(char_type* s,
                                           streamsize n) override;
    pos_type seekoff(off_type off, ios_base::seekdir way,
                     ios_base::openmode which
                      = ios_base::in | ios_base::out) override;
    pos_type seekpos(pos_type sp,
                     ios_base::openmode which
                      = ios_base::in | ios_base::out) override;
    int      sync() override;
    void     imbue(const locale& loc) override;
  };

  template<class charT, class traits>
    void swap(basic_filebuf<charT, traits>& x,
              basic_filebuf<charT, traits>& y);
}
```

**1** The class `basic_filebuf<charT, traits>` associates both the input sequence and the output sequence with a file.

**2** The restrictions on reading and writing a sequence controlled by an object of class `basic_filebuf<charT, traits>` are the same as for reading and writing with the C standard library `FILE`s.

**3** In particular:

- If the file is not open for reading the input sequence cannot be read.
- If the file is not open for writing the output sequence cannot be written.
- A joint file position is maintained for both the input sequence and the output sequence.

**4** An instance of `basic_filebuf` behaves as described in [[filebuf]](filebuf.md) provided `traits::pos_type` is `fpos<traits::state_type>`. Otherwise the behavior is undefined.

**5** In order to support file I/O and multibyte/wide character conversion, conversions are performed using members of a facet, referred to as `a_codecvt` in following subclauses, obtained as if by

```cpp
const codecvt<charT, char, typename traits::state_type>& a_codecvt =
  use_facet<codecvt<charT, char, typename traits::state_type>>(getloc());
```

## Subclauses

- [29.9.2.1 Constructors \[filebuf.cons\]](filebuf.cons.md)
- [29.9.2.2 Assignment and swap \[filebuf.assign\]](filebuf.assign.md)
- [29.9.2.3 Member functions \[filebuf.members\]](filebuf.members.md)
- [29.9.2.4 Overridden virtual functions \[filebuf.virtuals\]](filebuf.virtuals.md)
