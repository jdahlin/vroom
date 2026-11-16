# C.3.2 [[basic]](../basic/basic.md): basics [diff.cpp11.basic]

[Contents](../README.md) · up: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · prev: [C.3.1 [[lex]](../lex/lex.md): lexical conventions \[diff.cpp11.lex\]](diff.cpp11.lex.md) · next: [C.3.3 [[expr]](../expr/expr.md): expressions \[diff.cpp11.expr\]](diff.cpp11.expr.md)

**1** **Affected subclause:** [[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md) <br>**Change:** New usual (non-placement) deallocator. <br>**Rationale:** Required for sized deallocation. <br>**Effect on original feature:** Valid C++ 2011 code could declare a global placement allocation function and deallocation function as follows:

```cpp
void* operator new(std::size_t, std::size_t);
void operator delete(void*, std::size_t) noexcept;
```

In this International Standard, however, the declaration of `operator delete` might match a predefined usual (non-placement) `operator delete` ([[basic.stc.dynamic]](../basic/basic.stc.dynamic.md)). If so, the program is ill-formed, as it was for class member allocation functions and deallocation functions ([[expr.new]](../expr/expr.new.md)).
