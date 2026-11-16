# C.2.10 [[containers]](../lib/containers/containers.md): containers library [diff.cpp14.containers]

[Contents](../README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.9 [[strings]](../lib/strings/strings.md): strings library \[diff.cpp14.string\]](diff.cpp14.string.md) · next: [C.2.11 [[depr]](../depr/depr.md): compatibility features \[diff.cpp14.depr\]](diff.cpp14.depr.md)

**1** **Affected subclause:** [[associative.reqmts]](../lib/containers/associative.reqmts.md) <br>**Change:** Requirements change: <br>**Rationale:** Increase portability, clarification of associative container requirements. <br>**Effect on original feature:** Valid C++ 2014 code that attempts to use associative containers having a comparison object with non-const function call operator may fail to compile in this International Standard:

```cpp
#include <set>

struct compare
{
  bool operator()(int a, int b)
  {
    return a < b;
  }
};

int main() {
  const std::set<int, compare> s;
  s.find(0);
}
```
