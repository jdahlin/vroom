# 29.9.4 Class template `basic_ofstream` [ofstream]

[Contents](../README.md) · up: [29.9 File-based streams \[file.streams\]](file.streams.md) · prev: [29.9.3.3 Member functions \[ifstream.members\]](ifstream.members.md) · next: [29.9.4.1 Constructors \[ofstream.cons\]](ofstream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_ofstream : public basic_ostream<charT, traits> {
  public:
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [ofstream.cons], constructors
    basic_ofstream();
    explicit basic_ofstream(const char* s,
                            ios_base::openmode mode = ios_base::out);
    explicit basic_ofstream(const filesystem::path::value_type* s,  // wide systems only; see [fstream.syn]
                            ios_base::openmode mode = ios_base::out);
    explicit basic_ofstream(const string& s,
                            ios_base::openmode mode = ios_base::out);
    explicit basic_ofstream(const filesystem::path& s,
                            ios_base::openmode mode = ios_base::out);
    basic_ofstream(const basic_ofstream&) = delete;
    basic_ofstream(basic_ofstream&& rhs);

    // [ofstream.assign], assign and swap
    basic_ofstream& operator=(const basic_ofstream&) = delete;
    basic_ofstream& operator=(basic_ofstream&& rhs);
    void swap(basic_ofstream& rhs);

    // [ofstream.members], members
    basic_filebuf<charT, traits>* rdbuf() const;

    bool is_open() const;
    void open(const char* s, ios_base::openmode mode = ios_base::out);
    void open(const filesystem::path::value_type* s,
              ios_base::openmode mode = ios_base::out);     // wide systems only; see [fstream.syn]
    void open(const string& s, ios_base::openmode mode = ios_base::out);
    void open(const filesystem::path& s, ios_base::openmode mode = ios_base::out);
    void close();
  private:
    basic_filebuf<charT, traits> sb;    // exposition only
  };

  template<class charT, class traits>
    void swap(basic_ofstream<charT, traits>& x,
              basic_ofstream<charT, traits>& y);
}
```

**1** The class `basic_ofstream<charT, traits>` supports writing to named files. It uses a `basic_filebuf<charT, traits>` object to control the associated sequence. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `filebuf` object.

## Subclauses

- [29.9.4.1 Constructors \[ofstream.cons\]](ofstream.cons.md)
- [29.9.4.2 Assignment and swap \[ofstream.assign\]](ofstream.assign.md)
- [29.9.4.3 Member functions \[ofstream.members\]](ofstream.members.md)
