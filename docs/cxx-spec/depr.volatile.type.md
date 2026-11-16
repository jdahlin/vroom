# D.5 Deprecated `volatile` types [depr.volatile.type]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.4 Array comparisons \[depr.array.comp\]](depr.array.comp.md) · next: [D.6 Redeclaration of `static constexpr` data members \[depr.static.constexpr\]](depr.static.constexpr.md)

**1** Postfix `++` and `--` expressions ([[expr.post.incr]](expr.post.incr.md)) and prefix `++` and `--` expressions ([[expr.pre.incr]](expr.pre.incr.md)) of volatile-qualified arithmetic and pointer types are deprecated.

[*Example 1*:

```cpp
volatile int velociraptor;
++velociraptor;                     // deprecated
```

— *end example*]

**2** Certain assignments where the left operand is a volatile-qualified non-class type are deprecated; see [[expr.ass]](expr.ass.md).

[*Example 2*:

```cpp
int neck, tail;
volatile int brachiosaur;
brachiosaur = neck;                 // OK
tail = brachiosaur;                 // OK
tail = brachiosaur = neck;          // deprecated
brachiosaur += neck;                // deprecated
brachiosaur = brachiosaur + neck;   // OK
```

— *end example*]

**3** A function type ([[dcl.fct]](dcl.fct.md)) with a parameter with volatile-qualified type or with a volatile-qualified return type is deprecated.

[*Example 3*:

```cpp
volatile struct amber jurassic();                               // deprecated
void trex(volatile short left_arm, volatile short right_arm);   // deprecated
void fly(volatile struct pterosaur* pteranodon);                // OK
```

— *end example*]

**4** A structured binding ([[dcl.struct.bind]](dcl.struct.bind.md)) of a volatile-qualified type is deprecated.

[*Example 4*:

```cpp
struct linhenykus { short forelimb; };
void park(linhenykus alvarezsauroid) {
  volatile auto [what_is_this] = alvarezsauroid;                // deprecated
  // ...
}
```

— *end example*]
