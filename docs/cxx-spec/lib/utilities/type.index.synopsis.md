# 20.17.1 Header `<typeindex>` synopsis [type.index.synopsis]

[Contents](../../README.md) · up: [20.17 Class `type_index` \[type.index\]](type.index.md) · prev: [20.17 Class `type_index` \[type.index\]](type.index.md) · next: [20.17.2 `type_index` overview \[type.index.overview\]](type.index.overview.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  class type_index;
  template<class T> struct hash;
  template<> struct hash<type_index>;
}
```
