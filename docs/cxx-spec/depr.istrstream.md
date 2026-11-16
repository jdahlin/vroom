# D.12.3 Class `istrstream` [depr.istrstream]

[Contents](README.md) · up: [D.12 `char*` streams \[depr.str.strstreams\]](depr.str.strstreams.md) · prev: [D.12.2.3 `strstreambuf` overridden virtual functions \[depr.strstreambuf.virtuals\]](depr.strstreambuf.virtuals.md) · next: [D.12.3.1 `istrstream` constructors \[depr.istrstream.cons\]](depr.istrstream.cons.md)

```cpp
namespace std {
  class istrstream : public basic_istream<char> {
  public:
    explicit istrstream(const char* s);
    explicit istrstream(char* s);
    istrstream(const char* s, streamsize n);
    istrstream(char* s, streamsize n);
    virtual ~istrstream();

    strstreambuf* rdbuf() const;
    char* str();
  private:
    strstreambuf sb;            // exposition only
  };
}
```

**1** The class `istrstream` supports the reading of objects of class `strstreambuf`. It supplies a `strstreambuf` object to control the associated array object. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `strstreambuf` object.

## Subclauses

- [D.12.3.1 `istrstream` constructors \[depr.istrstream.cons\]](depr.istrstream.cons.md)
- [D.12.3.2 Member functions \[depr.istrstream.members\]](depr.istrstream.members.md)
