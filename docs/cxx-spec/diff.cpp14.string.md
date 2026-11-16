# C.2.9 [[strings]](lib/strings.md): strings library [diff.cpp14.string]

[Contents](README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.8 [[utilities]](lib/utilities.md): general utilities library \[diff.cpp14.utilities\]](diff.cpp14.utilities.md) · next: [C.2.10 [[containers]](lib/containers.md): containers library \[diff.cpp14.containers\]](diff.cpp14.containers.md)

**1** **Affected subclause:** [[basic.string]](lib/basic.string.md) <br>**Change:** Non-const `.data()` member added. <br>**Rationale:** The lack of a non-const `.data()` differed from the similar member of `std::vector`. This change regularizes behavior for this International Standard. <br>**Effect on original feature:** Overloaded functions which have differing code paths for `char*` and `const char*` arguments will execute differently when called with a non-const string’s `.data()` member in this International Standard.

```cpp
int f(char *) = delete;
int f(const char *);
string s;
int x = f(s.data());            // ill-formed; previously well-formed
```
