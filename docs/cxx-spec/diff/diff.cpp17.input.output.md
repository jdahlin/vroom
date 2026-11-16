# C.1.13 [[input.output]](../lib/io/input.output.md): input/output library [diff.cpp17.input.output]

[Contents](../README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.12 [[algorithms]](../lib/algorithms/algorithms.md): algorithms library \[diff.cpp17.alg.reqs\]](diff.cpp17.alg.reqs.md) · next: [C.1.14 [[depr]](../depr/depr.md): compatibility features \[diff.cpp17.depr\]](diff.cpp17.depr.md)

**1** **Affected subclause:** [[istream.extractors]](../lib/io/istream.extractors.md) <br>**Change:** Character array extraction only takes array types. <br>**Rationale:** Increase safety via preventing buffer overflow at compile time. <br>**Effect on original feature:** Valid C++ 2017 code may fail to compile in this International Standard:

```cpp
auto p = new char[100];
char q[100];
std::cin >> std::setw(20) >> p;         // ill-formed; previously well-formed
std::cin >> std::setw(20) >> q;         // OK
```

**2** **Affected subclause:** [[ostream.inserters.character]](../lib/io/ostream.inserters.character.md) <br>**Change:** Overload resolution for ostream inserters used with UTF-8 literals. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2017 code that passes UTF-8 literals to `basic_ostream<char, ...>::operator<<` or `basic_ostream<wchar_t, ...>::operator<<` is now ill-formed.

```cpp
std::cout << u8"text";          // previously called operator<<(const char*) and printed a string;
                                // now ill-formed
std::cout << u8'X';             // previously called operator<<(char) and printed a character;
                                // now ill-formed
```

**3** **Affected subclause:** [[ostream.inserters.character]](../lib/io/ostream.inserters.character.md) <br>**Change:** Overload resolution for ostream inserters used with `wchar_t`, `char16_t`, or `char32_t` types. <br>**Rationale:** Removal of surprising behavior. <br>**Effect on original feature:** Valid C++ 2017 code that passes `wchar_t`, `char16_t`, or `char32_t` characters or strings to `basic_ostream<char, ...>::operator<<` or that passes `char16_t` or `char32_t` characters or strings to `basic_ostream<wchar_t, ...>::operator<<` is now ill-formed.

```cpp
std::cout << u"text";           // previously formatted the string as a pointer value;
                                // now ill-formed
std::cout << u'X';              // previously formatted the character as an integer value;
                                // now ill-formed
```

**4** **Affected subclause:** [[fs.class.path]](../lib/io/fs.class.path.md) <br>**Change:** Return type of filesystem path format observer member functions. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2017 code that depends on the `u8string()` and `generic_u8string()` member functions of `std::filesystem::path` returning `std::string` is not valid in this International Standard.

```cpp
std::filesystem::path p;
std::string s1 = p.u8string();          // ill-formed; previously well-formed
std::string s2 = p.generic_u8string();  // ill-formed; previously well-formed
```
