# D.12.2.2 Member functions [depr.strstreambuf.members]

[Contents](README.md) · up: [D.12.2 Class `strstreambuf` \[depr.strstreambuf\]](depr.strstreambuf.md) · prev: [D.12.2.1 `strstreambuf` constructors \[depr.strstreambuf.cons\]](depr.strstreambuf.cons.md) · next: [D.12.2.3 `strstreambuf` overridden virtual functions \[depr.strstreambuf.virtuals\]](depr.strstreambuf.virtuals.md)

```cpp
void freeze(bool freezefl = true);
```

**1** *Effects:* If `strmode & dynamic` is nonzero, alters the freeze status of the dynamic array object as follows:

- If `freezefl` is `true`, the function sets `frozen` in `strmode`.
- Otherwise, it clears `frozen` in `strmode`.

```cpp
char* str();
```

**2** *Effects:* Calls `freeze()`, then returns the beginning pointer for the input sequence, `gbeg`.

**3** *Remarks:* The return value can be a null pointer.

```cpp
int pcount() const;
```

**4** *Effects:* If the next pointer for the output sequence, `pnext`, is a null pointer, returns zero. Otherwise, returns the current effective length of the array object as the next pointer minus the beginning pointer for the output sequence, `pnext - pbeg`.
