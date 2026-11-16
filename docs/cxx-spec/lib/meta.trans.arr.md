# 20.15.7.4 Array modifications [meta.trans.arr]

[Contents](../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7.3 Sign modifications \[meta.trans.sign\]](meta.trans.sign.md) · next: [20.15.7.5 Pointer modifications \[meta.trans.ptr\]](meta.trans.ptr.md)

<a id="tab:meta.trans.arr"></a>
**Table 55: Array modifications** [tab:meta.trans.arr]

| Template | Comments |
| --- | --- |
| `template<class T> struct remove_extent;` | If `T` names a type “array of `U`”, the member typedef `type` shall be `U`, otherwise `T`. [*Note 1*: For multidimensional arrays, only the first array dimension is removed. For a type “array of `const U`”, the resulting type is `const U`. — *end note*] |
| `template<class T> struct remove_all_extents;` | If `T` is “multi-dimensional array of `U`”, the resulting member typedef `type` is `U`, otherwise `T`. |

**1** [*Example 1*:

```cpp
// the following assertions hold:
assert((is_same_v<remove_extent_t<int>, int>));
assert((is_same_v<remove_extent_t<int[2]>, int>));
assert((is_same_v<remove_extent_t<int[2][3]>, int[3]>));
assert((is_same_v<remove_extent_t<int[][3]>, int[3]>));
```

— *end example*]

**2** [*Example 2*:

```cpp
// the following assertions hold:
assert((is_same_v<remove_all_extents_t<int>, int>));
assert((is_same_v<remove_all_extents_t<int[2]>, int>));
assert((is_same_v<remove_all_extents_t<int[2][3]>, int>));
assert((is_same_v<remove_all_extents_t<int[][3]>, int>));
```

— *end example*]
