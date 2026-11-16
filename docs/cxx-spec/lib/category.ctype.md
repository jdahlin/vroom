# 28.4.1 The `ctype` category [category.ctype]

[Contents](../README.md) · up: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md) · prev: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md) · next: [28.4.1.1 Class template `ctype` \[locale.ctype\]](locale.ctype.md)

```cpp
namespace std {
  class ctype_base {
  public:
    using mask = see below;

    // numeric values are for exposition only.
    static const mask space  = 1 << 0;
    static const mask print  = 1 << 1;
    static const mask cntrl  = 1 << 2;
    static const mask upper  = 1 << 3;
    static const mask lower  = 1 << 4;
    static const mask alpha  = 1 << 5;
    static const mask digit  = 1 << 6;
    static const mask punct  = 1 << 7;
    static const mask xdigit = 1 << 8;
    static const mask blank  = 1 << 9;
    static const mask alnum  = alpha | digit;
    static const mask graph  = alnum | punct;
  };
}
```

**1** The type `mask` is a bitmask type ([[bitmask.types]](bitmask.types.md)).

## Subclauses

- [28.4.1.1 Class template `ctype` \[locale.ctype\]](locale.ctype.md)
  - [28.4.1.1.1 `ctype` members \[locale.ctype.members\]](locale.ctype.members.md)
  - [28.4.1.1.2 `ctype` virtual functions \[locale.ctype.virtuals\]](locale.ctype.virtuals.md)
- [28.4.1.2 Class template `ctype_byname` \[locale.ctype.byname\]](locale.ctype.byname.md)
- [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md)
  - [28.4.1.3.1 Destructor \[facet.ctype.char.dtor\]](facet.ctype.char.dtor.md)
  - [28.4.1.3.2 Members \[facet.ctype.char.members\]](facet.ctype.char.members.md)
  - [28.4.1.3.3 Static members \[facet.ctype.char.statics\]](facet.ctype.char.statics.md)
  - [28.4.1.3.4 Virtual functions \[facet.ctype.char.virtuals\]](facet.ctype.char.virtuals.md)
- [28.4.1.4 Class template `codecvt` \[locale.codecvt\]](locale.codecvt.md)
  - [28.4.1.4.1 Members \[locale.codecvt.members\]](locale.codecvt.members.md)
  - [28.4.1.4.2 Virtual functions \[locale.codecvt.virtuals\]](locale.codecvt.virtuals.md)
- [28.4.1.5 Class template `codecvt_byname` \[locale.codecvt.byname\]](locale.codecvt.byname.md)
