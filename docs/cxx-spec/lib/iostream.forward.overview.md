# 29.3.2 Overview [iostream.forward.overview]

[Contents](../README.md) · up: [29.3 Forward declarations \[iostream.forward\]](iostream.forward.md) · prev: [29.3.1 Header `<iosfwd>` synopsis \[iosfwd.syn\]](iosfwd.syn.md) · next: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md)

**1** The class template specialization `basic_ios<charT, traits>` serves as a virtual base class for the class templates `basic_istream`, `basic_ostream`, and class templates derived from them. `basic_iostream` is a class template derived from both `basic_istream<charT, traits>` and `basic_ostream<charT, traits>`.

**2** The class template specialization `basic_streambuf<charT, traits>` serves as a base class for class templates `basic_stringbuf`, `basic_filebuf`, and `basic_syncbuf`.

**3** The class template specialization `basic_istream<charT, traits>` serves as a base class for class templates `basic_istringstream` and `basic_ifstream`.

**4** The class template specialization `basic_ostream<charT, traits>` serves as a base class for class templates `basic_ostringstream`, `basic_ofstream`, and `basic_osyncstream`.

**5** The class template specialization `basic_iostream<charT, traits>` serves as a base class for class templates `basic_stringstream` and `basic_fstream`.

**6** [*Note 1*: For each of the class templates above, the program is ill-formed if `traits::char_type` is not the same type as `charT` ([[char.traits]](char.traits.md)). — *end note*]

**7** Other *typedef-name*s define instances of class templates specialized for `char` or `wchar_t` types.

**8** Specializations of the class template `fpos` are used for specifying file position information. [*Example 1*: The types `streampos` and `wstreampos` are used for positioning streams specialized on `char` and `wchar_t` respectively. — *end example*]

**9** [*Note 2*: This synopsis suggests a circularity between `streampos` and `char_traits<char>`. An implementation can avoid this circularity by substituting equivalent types. — *end note*]
