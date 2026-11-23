# D.19 Deprecated `basic_string` capacity [depr.string.capacity]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.18 Deprecated `shared_ptr` atomic access \[depr.util.smartptr.shared.atomic\]](depr.util.smartptr.shared.atomic.md) · next: [D.20 Deprecated standard code conversion facets \[depr.locale.stdcvt\]](depr.locale.stdcvt.md)

**1** The following member is declared in addition to those members specified in [[string.capacity]](../lib/strings/string.capacity.md):

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_string {
  public:
    void reserve();
  };
}
```

```cpp
void reserve();
```

**2** *Effects:* After this call, `capacity()` has an unspecified value greater than or equal to `size()`. [*Note 1*: This is a non-binding shrink to fit request. — *end note*]
