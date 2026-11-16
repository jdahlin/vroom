# C.2.8 [[utilities]](lib/utilities.md): general utilities library [diff.cpp14.utilities]

[Contents](README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.7 [[library]](lib/library.md): library introduction \[diff.cpp14.library\]](diff.cpp14.library.md) · next: [C.2.9 [[strings]](lib/strings.md): strings library \[diff.cpp14.string\]](diff.cpp14.string.md)

**1** **Affected subclause:** [[func.wrap]](lib/func.wrap.md) <br>**Change:** Constructors taking allocators removed. <br>**Rationale:** No implementation consensus. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or may change meaning in this International Standard. Specifically, constructing a `std::function` with an allocator is ill-formed and uses-allocator construction will not pass an allocator to `std::function` constructors in this International Standard.

**2** **Affected subclause:** [[util.smartptr.shared]](lib/util.smartptr.shared.md) <br>**Change:** Different constraint on conversions from `unique_ptr`. <br>**Rationale:** Adding array support to `shared_ptr`, via the syntax `shared_ptr<T[]>` and `shared_ptr<T[N]>`. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or may change meaning in this International Standard. For example:

```cpp
#include <memory>
std::unique_ptr<int[]> arr(new int[1]);
std::shared_ptr<int> ptr(std::move(arr));   // error: int(*)[] is not compatible with int*
```
