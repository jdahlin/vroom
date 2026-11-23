# 21.5.3 Header `<cstring>` synopsis [cstring.syn]

[Contents](../../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5.2 Header `<cwctype>` synopsis \[cwctype.syn\]](cwctype.syn.md) · next: [21.5.4 Header `<cwchar>` synopsis \[cwchar.syn\]](cwchar.syn.md)

```cpp
namespace std {
  using size_t = see [support.types.layout];

  void* memcpy(void* s1, const void* s2, size_t n);
  void* memmove(void* s1, const void* s2, size_t n);
  char* strcpy(char* s1, const char* s2);
  char* strncpy(char* s1, const char* s2, size_t n);
  char* strcat(char* s1, const char* s2);
  char* strncat(char* s1, const char* s2, size_t n);
  int memcmp(const void* s1, const void* s2, size_t n);
  int strcmp(const char* s1, const char* s2);
  int strcoll(const char* s1, const char* s2);
  int strncmp(const char* s1, const char* s2, size_t n);
  size_t strxfrm(char* s1, const char* s2, size_t n);
  const void* memchr(const void* s, int c, size_t n);           // see [library.c]
  void* memchr(void* s, int c, size_t n);                       // see [library.c]
  const char* strchr(const char* s, int c);                     // see [library.c]
  char* strchr(char* s, int c);                                 // see [library.c]
  size_t strcspn(const char* s1, const char* s2);
  const char* strpbrk(const char* s1, const char* s2);          // see [library.c]
  char* strpbrk(char* s1, const char* s2);                      // see [library.c]
  const char* strrchr(const char* s, int c);                    // see [library.c]
  char* strrchr(char* s, int c);                                // see [library.c]
  size_t strspn(const char* s1, const char* s2);
  const char* strstr(const char* s1, const char* s2);           // see [library.c]
  char* strstr(char* s1, const char* s2);                       // see [library.c]
  char* strtok(char* s1, const char* s2);
  void* memset(void* s, int c, size_t n);
  char* strerror(int errnum);
  size_t strlen(const char* s);
}

#define NULL see [support.types.nullptr]
```

**1** The contents and meaning of the header `<cstring>` are the same as the C standard library header `<string.h>`.

**2** The functions `strerror` and `strtok` are not required to avoid data races ([[res.on.data.races]](../library/res.on.data.races.md)).

**3** The functions `memcpy` and `memmove` are signal-safe ([[support.signal]](../support/support.signal.md)). Both functions implicitly create objects ([[intro.object]](../../basic/intro.object.md)) in the destination region of storage immediately prior to copying the sequence of characters to the destination.

**4** [*Note 1*: The functions `strchr`, `strpbrk`, `strrchr`, `strstr`, and `memchr`, have different signatures in this document, but they have the same behavior as in the C standard library ([[library.c]](../library/library.c.md)). — *end note*]

*See also:* ISO C 7.24
