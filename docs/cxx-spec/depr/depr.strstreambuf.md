# D.12.2 Class `strstreambuf` [depr.strstreambuf]

[Contents](../README.md) · up: [D.12 `char*` streams \[depr.str.strstreams\]](depr.str.strstreams.md) · prev: [D.12.1 Header `<strstream>` synopsis \[depr.strstream.syn\]](depr.strstream.syn.md) · next: [D.12.2.1 `strstreambuf` constructors \[depr.strstreambuf.cons\]](depr.strstreambuf.cons.md)

```cpp
namespace std {
  class strstreambuf : public basic_streambuf<char> {
  public:
    strstreambuf() : strstreambuf(0) {}
    explicit strstreambuf(streamsize alsize_arg);
    strstreambuf(void* (*palloc_arg)(size_t), void (*pfree_arg)(void*));
    strstreambuf(char* gnext_arg, streamsize n, char* pbeg_arg = nullptr);
    strstreambuf(const char* gnext_arg, streamsize n);

    strstreambuf(signed char* gnext_arg, streamsize n,
                 signed char* pbeg_arg = nullptr);
    strstreambuf(const signed char* gnext_arg, streamsize n);
    strstreambuf(unsigned char* gnext_arg, streamsize n,
                 unsigned char* pbeg_arg = nullptr);
    strstreambuf(const unsigned char* gnext_arg, streamsize n);

    virtual ~strstreambuf();

    void  freeze(bool freezefl = true);
    char* str();
    int   pcount();

  protected:
    int_type overflow (int_type c = EOF) override;
    int_type pbackfail(int_type c = EOF) override;
    int_type underflow() override;
    pos_type seekoff(off_type off, ios_base::seekdir way,
                     ios_base::openmode which = ios_base::in | ios_base::out) override;
    pos_type seekpos(pos_type sp,
                     ios_base::openmode which = ios_base::in | ios_base::out) override;
    streambuf* setbuf(char* s, streamsize n) override;

  private:
    using strstate = T1;                // exposition only
    static const strstate allocated;    // exposition only
    static const strstate constant;     // exposition only
    static const strstate dynamic;      // exposition only
    static const strstate frozen;       // exposition only
    strstate strmode;                   // exposition only
    streamsize alsize;                  // exposition only
    void* (*palloc)(size_t);            // exposition only
    void (*pfree)(void*);               // exposition only
  };
}
```

**1** The class `strstreambuf` associates the input sequence, and possibly the output sequence, with an object of some *character* array type, whose elements store arbitrary values. The array object has several attributes.

**2** [*Note 1*: For the sake of exposition, these are represented as elements of a bitmask type (indicated here as `T1`) called `strstate`. The elements are:

- `allocated`, set when a dynamic array object has been allocated, and hence should be freed by the destructor for the `strstreambuf` object;
- `constant`, set when the array object has `const` elements, so the output sequence cannot be written;
- `dynamic`, set when the array object is allocated (or reallocated) as necessary to hold a character sequence that can change in length;
- `frozen`, set when the program has requested that the array object not be altered, reallocated, or freed.

— *end note*]

**3** [*Note 2*: For the sake of exposition, the maintained data is presented here as:

- `strstate strmode`, the attributes of the array object associated with the `strstreambuf` object;
- `int alsize`, the suggested minimum size for a dynamic array object;
- `void* (*palloc)(size_t)`, points to the function to call to allocate a dynamic array object;
- `void (*pfree)(void*)`, points to the function to call to free a dynamic array object.

— *end note*]

**4** Each object of class `strstreambuf` has a *seekable area*, delimited by the pointers `seeklow` and `seekhigh`. If `gnext` is a null pointer, the seekable area is undefined. Otherwise, `seeklow` equals `gbeg` and `seekhigh` is either `pend`, if `pend` is not a null pointer, or `gend`.

## Subclauses

- [D.12.2.1 `strstreambuf` constructors \[depr.strstreambuf.cons\]](depr.strstreambuf.cons.md)
- [D.12.2.2 Member functions \[depr.strstreambuf.members\]](depr.strstreambuf.members.md)
- [D.12.2.3 `strstreambuf` overridden virtual functions \[depr.strstreambuf.virtuals\]](depr.strstreambuf.virtuals.md)
