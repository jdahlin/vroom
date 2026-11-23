# 29.9.5 Class template `basic_fstream` [fstream]

[Contents](../../README.md) · up: [29.9 File-based streams \[file.streams\]](file.streams.md) · prev: [29.9.4.3 Member functions \[ofstream.members\]](ofstream.members.md) · next: [29.9.5.1 Constructors \[fstream.cons\]](fstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_fstream : public basic_iostream<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [fstream.cons], constructors
    basic_fstream();
    explicit basic_fstream(
      const char* s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    explicit basic_fstream(
      const filesystem::path::value_type* s,
      ios_base::openmode mode = ios_base::in|ios_base::out);    // wide systems only; see [fstream.syn]
    explicit basic_fstream(
      const string& s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    explicit basic_fstream(
      const filesystem::path& s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    basic_fstream(const basic_fstream&) = delete;
    basic_fstream(basic_fstream&& rhs);

    // [fstream.assign], assign and swap
    basic_fstream& operator=(const basic_fstream&) = delete;
    basic_fstream& operator=(basic_fstream&& rhs);
    void swap(basic_fstream& rhs);

    // [fstream.members], members
    basic_filebuf<charT, traits>* rdbuf() const;
    bool is_open() const;
    void open(
      const char* s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    void open(
      const filesystem::path::value_type* s,
      ios_base::openmode mode = ios_base::in|ios_base::out);    // wide systems only; see [fstream.syn]
    void open(
      const string& s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    void open(
      const filesystem::path& s,
      ios_base::openmode mode = ios_base::in | ios_base::out);
    void close();

  private:
    basic_filebuf<charT, traits> sb;    // exposition only
  };

  template<class charT, class traits>
    void swap(basic_fstream<charT, traits>& x,
              basic_fstream<charT, traits>& y);
}
```

**1** The class template `basic_fstream<charT, traits>` supports reading and writing from named files. It uses a `basic_filebuf<charT, traits>` object to control the associated sequences. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `basic_filebuf` object.

## Subclauses

- [29.9.5.1 Constructors \[fstream.cons\]](fstream.cons.md)
- [29.9.5.2 Assignment and swap \[fstream.assign\]](fstream.assign.md)
- [29.9.5.3 Member functions \[fstream.members\]](fstream.members.md)
