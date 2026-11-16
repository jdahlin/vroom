# 29.5.3.1.6 Class `ios_base::Init` [ios.init]

[Contents](../../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1.5 Type `ios_base::seekdir` \[ios.seekdir\]](ios.seekdir.md) · next: [29.5.3.2 State functions \[fmtflags.state\]](fmtflags.state.md)

```cpp
namespace std {
  class ios_base::Init {
  public:
    Init();
    Init(const Init&) = default;
    ~Init();
    Init& operator=(const Init&) = default;
  private:
    static int init_cnt;        // exposition only
  };
}
```

**1** The class `Init` describes an object whose construction ensures the construction of the eight objects declared in `<iostream>` ([[iostream.objects]](iostream.objects.md)) that associate file stream buffers with the standard C streams provided for by the functions declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**2** For the sake of exposition, the maintained data is presented here as:

- `static int init_cnt`, counts the number of constructor and destructor calls for class `Init`, initialized to zero.

```cpp
Init();
```

**3** *Effects:* Constructs and initializes the objects `cin`, `cout`, `cerr`, `clog`, `wcin`, `wcout`, `wcerr`, and `wclog` if they have not already been constructed and initialized.

```cpp
~Init();
```

**4** *Effects:* If there are no other instances of the class still in existence, calls `cout.flush()`, `cerr.flush()`, `clog.flush()`, `wcout.flush()`, `wcerr.flush()`, `wclog.flush()`.
