# 17.9.2 Class `exception` [exception]

[Contents](../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9.1 Header `<exception>` synopsis \[exception.syn\]](exception.syn.md) · next: [17.9.3 Class `bad_exception` \[bad.exception\]](bad.exception.md)

```cpp
namespace std {
  class exception {
  public:
    exception() noexcept;
    exception(const exception&) noexcept;
    exception& operator=(const exception&) noexcept;
    virtual ~exception();
    virtual const char* what() const noexcept;
  };
}
```

**1** The class `exception` defines the base class for the types of objects thrown as exceptions by C++ standard library components, and certain expressions, to report errors detected during program execution.

**2** Each standard library class `T` that derives from class `exception` has the following publicly accessible member functions, each of them having a non-throwing exception specification ([[except.spec]](../except.spec.md)):

- default constructor (unless the class synopsis shows other constructors)
- copy constructor
- copy assignment operator

The copy constructor and the copy assignment operator meet the following postcondition: If two objects `lhs` and `rhs` both have dynamic type `T` and `lhs` is a copy of `rhs`, then `strcmp(lhs.what(), rhs.what())` is equal to `0`. The `what()` member function of each such `T` satisfies the constraints specified for `exception::what()` (see below).

```cpp
exception(const exception& rhs) noexcept;
exception& operator=(const exception& rhs) noexcept;
```

**3** *Postconditions:* If `*this` and `rhs` both have dynamic type `exception` then the value of the expression `strcmp(what(), rhs.what())` shall equal 0.

```cpp
virtual ~exception();
```

**4** *Effects:* Destroys an object of class `exception`.

```cpp
virtual const char* what() const noexcept;
```

**5** *Returns:* An implementation-defined NTBS.

**6** *Remarks:* The message may be a null-terminated multibyte string ([[multibyte.strings]](multibyte.strings.md)), suitable for conversion and display as a `wstring` ([[string.classes]](string.classes.md), [[locale.codecvt]](locale.codecvt.md)). The return value remains valid until the exception object from which it is obtained is destroyed or a non-`const` member function of the exception object is called.
