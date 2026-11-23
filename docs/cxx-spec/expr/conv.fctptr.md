# 7.3.13 Function pointer conversions [conv.fctptr]

[Contents](../README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.12 Pointer-to-member conversions \[conv.mem\]](conv.mem.md) · next: [7.3.14 Boolean conversions \[conv.bool\]](conv.bool.md)

**1** A prvalue of type “pointer to `noexcept` function” can be converted to a prvalue of type “pointer to function”. The result is a pointer to the function. A prvalue of type “pointer to member of type `noexcept` function” can be converted to a prvalue of type “pointer to member of type function”. The result designates the member function.

[*Example 1*:

```cpp
void (*p)();
void (**pp)() noexcept = &p;    // error: cannot convert to pointer to noexcept function

struct S { typedef void (*p)(); operator p(); };
void (*q)() noexcept = S();     // error: cannot convert to pointer to noexcept function
```

— *end example*]
