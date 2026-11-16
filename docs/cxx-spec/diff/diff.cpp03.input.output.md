# C.4.14 [[input.output]](../lib/io/input.output.md): input/output library [diff.cpp03.input.output]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.13 [[numerics]](../lib/numerics/numerics.md): numerics library \[diff.cpp03.numerics\]](diff.cpp03.numerics.md) · next: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md)

**1** **Affected subclauses:** [[istream.sentry]](../lib/io/istream.sentry.md), [[ostream.sentry]](../lib/io/ostream.sentry.md), [[iostate.flags]](../lib/io/iostate.flags.md) <br>**Change:** Specify use of `explicit` in existing boolean conversion functions. <br>**Rationale:** Clarify intentions, avoid workarounds. <br>**Effect on original feature:** Valid C++ 2003 code that relies on implicit boolean conversions will fail to compile with this International Standard. Such conversions occur in the following conditions:

- passing a value to a function that takes an argument of type `bool`;
- using `operator==` to compare to `false` or `true`;
- returning a value from a function with a return type of `bool`;
- initializing members of type `bool` via aggregate initialization;
- initializing a `const bool&` which would bind to a temporary object.

**2** **Affected subclause:** [[ios.failure]](../lib/io/ios.failure.md) <br>**Change:** Change base class of `std::ios_base::failure`. <br>**Rationale:** More detailed error messages. <br>**Effect on original feature:** `std::ios_base::failure` is no longer derived directly from `std::exception`, but is now derived from `std::system_error`, which in turn is derived from `std::runtime_error`. Valid C++ 2003 code that assumes that `std::ios_base::failure` is derived directly from `std::exception` may execute differently in this International Standard.

**3** **Affected subclause:** [[ios.base]](../lib/io/ios.base.md) <br>**Change:** Flag types in `std::ios_base` are now bitmasks with values defined as constexpr static members. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2003 code that relies on `std::ios_base` flag types being represented as `std::bitset` or as an integer type may fail to compile with this International Standard. For example:

```cpp
#include <iostream>

int main() {
  int flag = std::ios_base::hex;
  std::cout.setf(flag);         // error: setf does not take argument of type int
}
```
