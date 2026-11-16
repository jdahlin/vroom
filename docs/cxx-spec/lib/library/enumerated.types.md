# 16.4.2.2.3 Enumerated types [enumerated.types]

[Contents](../../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2.2 Exposition-only types \[expos.only.types\]](expos.only.types.md) · next: [16.4.2.2.4 Bitmask types \[bitmask.types\]](bitmask.types.md)

**1** Several types defined in [[input.output]](../io/input.output.md) are *enumerated types*. Each enumerated type may be implemented as an enumeration or as a synonym for an enumeration.[^1]

**2** The enumerated type `enumerated` can be written:

```cpp
enum enumerated { V_0, V_1, V_2, V_3, ... };

inline const enumerated C_0(V_0);
inline const enumerated C_1(V_1);
inline const enumerated C_2(V_2);
inline const enumerated C_3(V_3);
  ⋮
```

**3** Here, the names `C`<sub>0</sub>, `C`<sub>1</sub>, etc. represent *enumerated elements* for this particular enumerated type. All such elements have distinct values.

[^1]: Such as an integer type, with constant integer values ([[basic.fundamental]](../../basic/basic.fundamental.md)).
