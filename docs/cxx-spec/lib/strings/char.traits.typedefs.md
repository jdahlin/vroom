# 21.2.2 Traits typedefs [char.traits.typedefs]

[Contents](../../README.md) · up: [21.2 Character traits \[char.traits\]](char.traits.md) · prev: [21.2.1 Character traits requirements \[char.traits.require\]](char.traits.require.md) · next: [21.2.3 `char_traits` specializations \[char.traits.specializations\]](char.traits.specializations.md)

```cpp
using int_type = see below;
```

**1** *Preconditions:* `int_type` shall be able to represent all of the valid characters converted from the corresponding `char_type` values, as well as an end-of-file value, `eof()`.[^1]

```cpp
using state_type = see below;
```

**2** *Preconditions:* `state_type` meets the *Cpp17Destructible* ([Table 32](../library/utility.arg.requirements.md#tab:cpp17.destructible)), *Cpp17CopyAssignable* ([Table 31](../library/utility.arg.requirements.md#tab:cpp17.copyassignable)), *Cpp17CopyConstructible* ([Table 29](../library/utility.arg.requirements.md#tab:cpp17.copyconstructible)), and *Cpp17DefaultConstructible* ([Table 27](../library/utility.arg.requirements.md#tab:cpp17.defaultconstructible)) requirements.

[^1]: If `eof()` can be held in `char_type` then some iostreams operations can give surprising results.
