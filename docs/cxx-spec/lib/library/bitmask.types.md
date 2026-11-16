# 16.4.2.2.4 Bitmask types [bitmask.types]

[Contents](../../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2.3 Enumerated types \[enumerated.types\]](enumerated.types.md) · next: [16.4.2.2.5 Character sequences \[character.seq\]](character.seq.md)

**1** Several types defined in [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md) are *bitmask types*. Each bitmask type can be implemented as an enumerated type that overloads certain operators, as an integer type, or as a `bitset` ([[template.bitset]](../utilities/template.bitset.md)).

**2** The bitmask type `bitmask` can be written:

```cpp
// For exposition only.
// int_type is an integral type capable of representing all values of the bitmask type.
enum bitmask : int_type {
  V_0 = 1 << 0, V_1 = 1 << 1, V_2 = 1 << 2, V_3 = 1 << 3, ...
};

inline constexpr bitmask C_0(V_0);
inline constexpr bitmask C_1(V_1);
inline constexpr bitmask C_2(V_2);
inline constexpr bitmask C_3(V_3);
  ⋮

constexpr bitmask operator&(bitmask X, bitmask Y) {
  return static_cast<bitmask>(
    static_cast<int_type>(X) & static_cast<int_type>(Y));
}
constexpr bitmask operator|(bitmask X, bitmask Y) {
  return static_cast<bitmask>(
    static_cast<int_type>(X) | static_cast<int_type>(Y));
}
constexpr bitmask operator^(bitmask X, bitmask Y){
  return static_cast<bitmask>(
    static_cast<int_type>(X) ^ static_cast<int_type>(Y));
}
constexpr bitmask operator~(bitmask X){
  return static_cast<bitmask>(~static_cast<int_type>(X));
}
bitmask& operator&=(bitmask& X, bitmask Y){
  X = X & Y; return X;
}
bitmask& operator|=(bitmask& X, bitmask Y) {
  X = X | Y; return X;
}
bitmask& operator^=(bitmask& X, bitmask Y) {
  X = X ^ Y; return X;
}
```

**3** Here, the names `C`<sub>0</sub>, `C`<sub>1</sub>, etc. represent *bitmask elements* for this particular bitmask type. All such elements have distinct, nonzero values such that, for any pair `C`<sub>*i*</sub> and `C`<sub>*j*</sub> where *i* ≠ *j*, `C_i & C_i` is nonzero and `C_i & C_j` is zero. Additionally, the value `0` is used to represent an *empty bitmask*, in which no bitmask elements are set.

**4** The following terms apply to objects and values of bitmask types:

- To *set* a value *Y* in an object *X* is to evaluate the expression *X* `|=` *Y*.
- To *clear* a value *Y* in an object *X* is to evaluate the expression *X* `&= ~`*Y*.
- The value *Y* *is set* in the object *X* if the expression *X* `&` *Y* is nonzero.
