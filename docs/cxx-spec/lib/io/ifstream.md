# 29.9.3 Class template `basic_ifstream` [ifstream]

[Contents](../../README.md) · up: [29.9 File-based streams \[file.streams\]](file.streams.md) · prev: [29.9.2.4 Overridden virtual functions \[filebuf.virtuals\]](filebuf.virtuals.md) · next: [29.9.3.1 Constructors \[ifstream.cons\]](ifstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_ifstream : public basic_istream<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [ifstream.cons], constructors
    basic_ifstream();
    explicit basic_ifstream(const char* s,
                            ios_base::openmode mode = ios_base::in);
    explicit basic_ifstream(const filesystem::path::value_type* s,
                            ios_base::openmode mode = ios_base::in);// wide systems only; see [fstream.syn]
    explicit basic_ifstream(const string& s,
                            ios_base::openmode mode = ios_base::in);
    explicit basic_ifstream(const filesystem::path& s,
                            ios_base::openmode mode = ios_base::in);
    basic_ifstream(const basic_ifstream&) = delete;
    basic_ifstream(basic_ifstream&& rhs);

    // [ifstream.assign], assign and swap
    basic_ifstream& operator=(const basic_ifstream&) = delete;
    basic_ifstream& operator=(basic_ifstream&& rhs);
    void swap(basic_ifstream& rhs);

    // [ifstream.members], members
    basic_filebuf<charT, traits>* rdbuf() const;

    bool is_open() const;
    void open(const char* s, ios_base::openmode mode = ios_base::in);
    void open(const filesystem::path::value_type* s,
              ios_base::openmode mode = ios_base::in);  // wide systems only; see [fstream.syn]
    void open(const string& s, ios_base::openmode mode = ios_base::in);
    void open(const filesystem::path& s, ios_base::openmode mode = ios_base::in);
    void close();
  private:
    basic_filebuf<charT, traits> sb;    // exposition only
  };

  template<class charT, class traits>
    void swap(basic_ifstream<charT, traits>& x,
              basic_ifstream<charT, traits>& y);
}
```

**1** The class `basic_ifstream<charT, traits>` supports reading from named files. It uses a `basic_filebuf<charT, traits>` object to control the associated sequence. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `filebuf` object.

## Subclauses

- [29.9.3.1 Constructors \[ifstream.cons\]](ifstream.cons.md)
- [29.9.3.2 Assignment and swap \[ifstream.assign\]](ifstream.assign.md)
- [29.9.3.3 Member functions \[ifstream.members\]](ifstream.members.md)
