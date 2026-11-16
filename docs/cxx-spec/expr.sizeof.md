# 7.6.2.4 Sizeof [expr.sizeof]

[Contents](README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.3 Await \[expr.await\]](expr.await.md) · next: [7.6.2.5 Alignof \[expr.alignof\]](expr.alignof.md)

**1** The `sizeof` operator yields the number of bytes occupied by a non-potentially-overlapping object of the type of its operand. The operand is either an expression, which is an unevaluated operand ([[expr.prop]](expr.prop.md)), or a parenthesized *type-id*. The `sizeof` operator shall not be applied to an expression that has function or incomplete type, to the parenthesized name of such types, or to a glvalue that designates a bit-field. The result of `sizeof` applied to any of the narrow character types is `1`. The result of `sizeof` applied to any other fundamental type ([[basic.fundamental]](basic.fundamental.md)) is implementation-defined. [*Note 1*: In particular, `sizeof(bool)`, `sizeof(char16_t)`, `sizeof(char32_t)`, and `sizeof(wchar_t)` are implementation-defined.[^1] — *end note*] [*Note 2*: See [[intro.memory]](intro.memory.md) for the definition of byte and [[basic.types]](basic.types.md) for the definition of object representation. — *end note*]

**2** When applied to a reference type, the result is the size of the referenced type. When applied to a class, the result is the number of bytes in an object of that class including any padding required for placing objects of that type in an array. The result of applying `sizeof` to a potentially-overlapping subobject is the size of the type, not the size of the subobject.[^2] When applied to an array, the result is the total number of bytes in the array. This implies that the size of an array of *n* elements is *n* times the size of an element.

**3** The lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are not applied to the operand of `sizeof`. If the operand is a prvalue, the temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied.

**4** The identifier in a `sizeof...` expression shall name a pack. The `sizeof...` operator yields the number of elements in the pack ([[temp.variadic]](temp.variadic.md)). A `sizeof...` expression is a pack expansion ([[temp.variadic]](temp.variadic.md)). [*Example 1*:

```cpp
template<class... Types>
struct count {
  static const std::size_t value = sizeof...(Types);
};
```

— *end example*]

**5** The result of `sizeof` and `sizeof...` is a prvalue of type `std::size_t`. [*Note 3*: A `sizeof` expression is an integral constant expression ([[expr.const]](expr.const.md)). The type `std::size_t` is defined in the standard header `<cstddef>` ([[cstddef.syn]](lib/cstddef.syn.md), [[support.types.layout]](lib/support.types.layout.md)). — *end note*]

[^1]: `sizeof(bool)` is not required to be `1`.
[^2]: The actual size of a potentially-overlapping subobject may be less than the result of applying `sizeof` to the subobject, due to virtual base classes and less strict padding requirements on potentially-overlapping subobjects.
