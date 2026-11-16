# D.12.5 Class `strstream` [depr.strstream]

[Contents](README.md) · up: [D.12 `char*` streams \[depr.str.strstreams\]](depr.str.strstreams.md) · prev: [D.12.4.2 Member functions \[depr.ostrstream.members\]](depr.ostrstream.members.md) · next: [D.12.5.1 `strstream` constructors \[depr.strstream.cons\]](depr.strstream.cons.md)

```cpp
namespace std {
  class strstream
    : public basic_iostream<char> {
  public:
    // types
    using char_type = char;
    using int_type  = char_traits<char>::int_type;
    using pos_type  = char_traits<char>::pos_type;
    using off_type  = char_traits<char>::off_type;

    // constructors/destructor
    strstream();
    strstream(char* s, int n,
              ios_base::openmode mode = ios_base::in|ios_base::out);
    virtual ~strstream();

    // members
    strstreambuf* rdbuf() const;
    void freeze(bool freezefl = true);
    int pcount() const;
    char* str();

  private:
    strstreambuf sb;            // exposition only
  };
}
```

**1** The class `strstream` supports reading and writing from objects of class `strstreambuf`. It supplies a `strstreambuf` object to control the associated array object. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `strstreambuf` object.

## Subclauses

- [D.12.5.1 `strstream` constructors \[depr.strstream.cons\]](depr.strstream.cons.md)
- [D.12.5.2 `strstream` destructor \[depr.strstream.dest\]](depr.strstream.dest.md)
- [D.12.5.3 `strstream` operations \[depr.strstream.oper\]](depr.strstream.oper.md)
