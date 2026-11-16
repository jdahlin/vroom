# 28.4.1.3.4 Virtual functions [facet.ctype.char.virtuals]

[Contents](../../README.md) · up: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md) · prev: [28.4.1.3.3 Static members \[facet.ctype.char.statics\]](facet.ctype.char.statics.md) · next: [28.4.1.4 Class template `codecvt` \[locale.codecvt\]](locale.codecvt.md)

```cpp
char        do_toupper(char) const;
const char* do_toupper(char* low, const char* high) const;
char        do_tolower(char) const;
const char* do_tolower(char* low, const char* high) const;

virtual char        do_widen(char c) const;
virtual const char* do_widen(const char* low, const char* high, char* to) const;
virtual char        do_narrow(char c, char dfault) const;
virtual const char* do_narrow(const char* low, const char* high,
                              char dfault, char* to) const;
```

**1** These functions are described identically as those members of the same name in the `ctype` class template ([[locale.ctype.members]](locale.ctype.members.md)).
