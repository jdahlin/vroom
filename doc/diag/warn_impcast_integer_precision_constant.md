# A constant that does not fit its destination

An integer constant is converted to a narrower type that cannot hold it, so its value
changes (it wraps).  The help line gives the range the type holds and the value it ends
up with.  Plain `char` is unsigned on Arm and RISC-V Linux and signed on Apple and x86,
so a constant above 127 or below 0 may fit on one target only: say `signed char` or
`unsigned char` when the sign matters.

A constant written in hexadecimal, octal or binary that fits the width as unsigned (`0xfe`
in a `char`) is taken as a bit pattern and does not warn.

```c wrong
unsigned char level = 300;
```

```c fixed
unsigned short level = 300;
```
