# 28.4.1.3.1 Destructor [facet.ctype.char.dtor]

[Contents](../../README.md) · up: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md) · prev: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md) · next: [28.4.1.3.2 Members \[facet.ctype.char.members\]](facet.ctype.char.members.md)

```cpp
~ctype();
```

**1** *Effects:* If the constructor’s first argument was nonzero, and its second argument was `true`, does `delete [] table()`.
