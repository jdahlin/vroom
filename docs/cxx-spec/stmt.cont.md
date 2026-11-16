# 8.7.2 The `continue` statement [stmt.cont]

[Contents](README.md) · up: [8.7 Jump statements \[stmt.jump\]](stmt.jump.md) · prev: [8.7.1 The `break` statement \[stmt.break\]](stmt.break.md) · next: [8.7.3 The `return` statement \[stmt.return\]](stmt.return.md)

**1** The `continue` statement shall occur only in an *iteration-statement* and causes control to pass to the loop-continuation portion of the smallest enclosing *iteration-statement*, that is, to the end of the loop. More precisely, in each of the statements

```cpp
while (foo) {
  {
    // ...
  }
contin: ;
}
```

```cpp
do {
  {
    // ...
  }
contin: ;
} while (foo);
```

```cpp
for (;;) {
  {
    // ...
  }
contin: ;
}
```

a `continue` not contained in an enclosed iteration statement is equivalent to `goto` `contin`.
