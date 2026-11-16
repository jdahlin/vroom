# 11.4.8.1 Static member functions [class.static.mfct]

[Contents](README.md) · up: [11.4.8 Static members \[class.static\]](class.static.md) · prev: [11.4.8 Static members \[class.static\]](class.static.md) · next: [11.4.8.2 Static data members \[class.static.data\]](class.static.data.md)

**1** [*Note 1*: The rules described in [[class.mfct]](class.mfct.md) apply to static member functions. — *end note*]

**2** [*Note 2*: A static member function does not have a `this` pointer ([[class.this]](class.this.md)). — *end note*] A static member function shall not be `virtual`. There shall not be a static and a non-static member function with the same name and the same parameter types ([[over.load]](over.load.md)). A static member function shall not be declared `const`, `volatile`, or `const volatile`.
