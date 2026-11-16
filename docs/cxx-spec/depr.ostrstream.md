# D.12.4 Class `ostrstream` [depr.ostrstream]

[Contents](README.md) · up: [D.12 `char*` streams \[depr.str.strstreams\]](depr.str.strstreams.md) · prev: [D.12.3.2 Member functions \[depr.istrstream.members\]](depr.istrstream.members.md) · next: [D.12.4.1 `ostrstream` constructors \[depr.ostrstream.cons\]](depr.ostrstream.cons.md)

```cpp
namespace std {
  class ostrstream : public basic_ostream<char> {
  public:
    ostrstream();
    ostrstream(char* s, int n, ios_base::openmode mode = ios_base::out);
    virtual ~ostrstream();

    strstreambuf* rdbuf() const;
    void freeze(bool freezefl = true);
    char* str();
    int pcount() const;
  private:
    strstreambuf sb;            // exposition only
  };
}
```

**1** The class `ostrstream` supports the writing of objects of class `strstreambuf`. It supplies a `strstreambuf` object to control the associated array object. For the sake of exposition, the maintained data is presented here as:

- `sb`, the `strstreambuf` object.

## Subclauses

- [D.12.4.1 `ostrstream` constructors \[depr.ostrstream.cons\]](depr.ostrstream.cons.md)
- [D.12.4.2 Member functions \[depr.ostrstream.members\]](depr.ostrstream.members.md)
