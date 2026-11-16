# 21.5.1 Header `<cctype>` synopsis [cctype.syn]

[Contents](../../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · next: [21.5.2 Header `<cwctype>` synopsis \[cwctype.syn\]](cwctype.syn.md)

```cpp
namespace std {
  int isalnum(int c);
  int isalpha(int c);
  int isblank(int c);
  int iscntrl(int c);
  int isdigit(int c);
  int isgraph(int c);
  int islower(int c);
  int isprint(int c);
  int ispunct(int c);
  int isspace(int c);
  int isupper(int c);
  int isxdigit(int c);
  int tolower(int c);
  int toupper(int c);
}
```

**1** The contents and meaning of the header `<cctype>` are the same as the C standard library header `<ctype.h>`.

*See also:* ISO C 7.4
