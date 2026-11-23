# 20.15.5 Type property queries [meta.unary.prop.query]

[Contents](../../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.4.3 Type properties \[meta.unary.prop\]](meta.unary.prop.md) · next: [20.15.6 Relationships between types \[meta.rel\]](meta.rel.md)

**1** This subclause contains templates that may be used to query properties of types at compile time.

<a id="tab:meta.unary.prop.query"></a>
**Table 50: Type property queries** [tab:meta.unary.prop.query]

| Template | Value |
| --- | --- |
| `template<class T> struct alignment_of;` | `alignof(T)`.<br>*Mandates:* `alignof(T)` is a valid expression ([[expr.alignof]](../../expr/expr.alignof.md)) |
| `template<class T> struct rank;` | If `T` names an array type, an integer value representing the number of dimensions of `T`; otherwise, 0. |
| `template<class T, unsigned I = 0> struct extent;` | If `T` is not an array type, or if it has rank less than or equal to `I`, or if `I` is 0 and `T` has type “array of unknown bound of `U`”, then 0; otherwise, the bound ([[dcl.array]](../../dcl/dcl.array.md)) of the `I`<sup></sup>*th* dimension of `T`, where indexing of `I` is zero-based |

**2** Each of these templates shall be a *Cpp17UnaryTypeTrait* ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `integral_constant<size_t, Value>`.

**3** [*Example 1*:

```cpp
// the following assertions hold:
assert(rank_v<int> == 0);
assert(rank_v<int[2]> == 1);
assert(rank_v<int[][4]> == 2);
```

— *end example*]

**4** [*Example 2*:

```cpp
// the following assertions hold:
assert(extent_v<int> == 0);
assert(extent_v<int[2]> == 2);
assert(extent_v<int[2][4]> == 2);
assert(extent_v<int[][4]> == 0);
assert((extent_v<int, 1>) == 0);
assert((extent_v<int[2], 1>) == 0);
assert((extent_v<int[2][4], 1>) == 4);
assert((extent_v<int[][4], 1>) == 4);
```

— *end example*]
