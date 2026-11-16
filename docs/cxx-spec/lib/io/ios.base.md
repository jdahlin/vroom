# 29.5.3 Class `ios_base` [ios.base]

[Contents](../../README.md) · up: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · prev: [29.5.2 Types \[stream.types\]](stream.types.md) · next: [29.5.3.1 Types \[ios.types\]](ios.types.md)

```cpp
namespace std {
  class ios_base {
  public:
    class failure;              // see below

    // [ios.fmtflags], fmtflags
    using fmtflags = T1;
    static constexpr fmtflags boolalpha = unspecified;
    static constexpr fmtflags dec = unspecified;
    static constexpr fmtflags fixed = unspecified;
    static constexpr fmtflags hex = unspecified;
    static constexpr fmtflags internal = unspecified;
    static constexpr fmtflags left = unspecified;
    static constexpr fmtflags oct = unspecified;
    static constexpr fmtflags right = unspecified;
    static constexpr fmtflags scientific = unspecified;
    static constexpr fmtflags showbase = unspecified;
    static constexpr fmtflags showpoint = unspecified;
    static constexpr fmtflags showpos = unspecified;
    static constexpr fmtflags skipws = unspecified;
    static constexpr fmtflags unitbuf = unspecified;
    static constexpr fmtflags uppercase = unspecified;
    static constexpr fmtflags adjustfield = see below;
    static constexpr fmtflags basefield = see below;
    static constexpr fmtflags floatfield = see below;

    // [ios.iostate], iostate
    using iostate = T2;
    static constexpr iostate badbit = unspecified;
    static constexpr iostate eofbit = unspecified;
    static constexpr iostate failbit = unspecified;
    static constexpr iostate goodbit = see below;

    // [ios.openmode], openmode
    using openmode = T3;
    static constexpr openmode app = unspecified;
    static constexpr openmode ate = unspecified;
    static constexpr openmode binary = unspecified;
    static constexpr openmode in = unspecified;
    static constexpr openmode out = unspecified;
    static constexpr openmode trunc = unspecified;

    // [ios.seekdir], seekdir
    using seekdir = T4;
    static constexpr seekdir beg = unspecified;
    static constexpr seekdir cur = unspecified;
    static constexpr seekdir end = unspecified;

    class Init;

    // [fmtflags.state], fmtflags state
    fmtflags flags() const;
    fmtflags flags(fmtflags fmtfl);
    fmtflags setf(fmtflags fmtfl);
    fmtflags setf(fmtflags fmtfl, fmtflags mask);
    void unsetf(fmtflags mask);

    streamsize precision() const;
    streamsize precision(streamsize prec);
    streamsize width() const;
    streamsize width(streamsize wide);

    // [ios.base.locales], locales
    locale imbue(const locale& loc);
    locale getloc() const;

    // [ios.base.storage], storage
    static int xalloc();
    long&  iword(int idx);
    void*& pword(int idx);

    // destructor
    virtual ~ios_base();

    // [ios.base.callback], callbacks
    enum event { erase_event, imbue_event, copyfmt_event };
    using event_callback = void (*)(event, ios_base&, int idx);
    void register_callback(event_callback fn, int idx);

    ios_base(const ios_base&) = delete;
    ios_base& operator=(const ios_base&) = delete;

    static bool sync_with_stdio(bool sync = true);

  protected:
    ios_base();

  private:
    static int index;           // exposition only
    long*  iarray;              // exposition only
    void** parray;              // exposition only
  };
}
```

**1** `ios_base` defines several member types:

- a type `failure`, defined as either a class derived from `system_error` or a synonym for a class derived from `system_error`;
- a class `Init`;
- three bitmask types, `fmtflags`, `iostate`, and `openmode`;
- an enumerated type, `seekdir`.

**2** It maintains several kinds of data:

- state information that reflects the integrity of the stream buffer;
- control information that influences how to interpret (format) input sequences and how to generate (format) output sequences;
- additional information that is stored by the program for its private use.

**3** [*Note 1*: For the sake of exposition, the maintained data is presented here as:

- `static int index`, specifies the next available unique index for the integer or pointer arrays maintained for the private use of the program, initialized to an unspecified value;
- `long* iarray`, points to the first element of an arbitrary-length `long` array maintained for the private use of the program;
- `void** parray`, points to the first element of an arbitrary-length pointer array maintained for the private use of the program.

— *end note*]

## Subclauses

- [29.5.3.1 Types \[ios.types\]](ios.types.md)
  - [29.5.3.1.1 Class `ios_base::failure` \[ios.failure\]](ios.failure.md)
  - [29.5.3.1.2 Type `ios_base::fmtflags` \[ios.fmtflags\]](ios.fmtflags.md)
  - [29.5.3.1.3 Type `ios_base::iostate` \[ios.iostate\]](ios.iostate.md)
  - [29.5.3.1.4 Type `ios_base::openmode` \[ios.openmode\]](ios.openmode.md)
  - [29.5.3.1.5 Type `ios_base::seekdir` \[ios.seekdir\]](ios.seekdir.md)
  - [29.5.3.1.6 Class `ios_base::Init` \[ios.init\]](ios.init.md)
- [29.5.3.2 State functions \[fmtflags.state\]](fmtflags.state.md)
- [29.5.3.3 Functions \[ios.base.locales\]](ios.base.locales.md)
- [29.5.3.4 Static members \[ios.members.static\]](ios.members.static.md)
- [29.5.3.5 Storage functions \[ios.base.storage\]](ios.base.storage.md)
- [29.5.3.6 Callbacks \[ios.base.callback\]](ios.base.callback.md)
- [29.5.3.7 Constructors and destructor \[ios.base.cons\]](ios.base.cons.md)
