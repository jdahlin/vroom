# 29.5.5.2 Constructors [basic.ios.cons]

[Contents](../../README.md) · up: [29.5.5 Class template `basic_ios` \[ios\]](ios.md) · prev: [29.5.5.1 Overview \[ios.overview\]](ios.overview.md) · next: [29.5.5.3 Member functions \[basic.ios.members\]](basic.ios.members.md)

```cpp
explicit basic_ios(basic_streambuf<charT, traits>* sb);
```

**1** *Effects:* Assigns initial values to its member objects by calling `init(sb)`.

```cpp
basic_ios();
```

**2** *Effects:* Leaves its member objects uninitialized. The object shall be initialized by calling `basic_ios::init` before its first use or before it is destroyed, whichever comes first; otherwise the behavior is undefined.

```cpp
~basic_ios();
```

**3** *Remarks:* The destructor does not destroy `rdbuf()`.

```cpp
void init(basic_streambuf<charT, traits>* sb);
```

**4** *Postconditions:* The postconditions of this function are indicated in [Table 122](basic.ios.cons.md#tab:basic.ios.cons).

<a id="tab:basic.ios.cons"></a>
**Table 122: `basic_ios::init()` effects** [tab:basic.ios.cons]

| Element | Value |
| --- | --- |
| `rdbuf()` | `sb` |
| `tie()` | `0` |
| `rdstate()` | `goodbit` if `sb` is not a null pointer, otherwise `badbit`. |
| `exceptions()` | `goodbit` |
| `flags()` | `skipws \| dec` |
| `width()` | `0` |
| `precision()` | `6` |
| `fill()` | `widen(' ')` |
| `getloc()` | a copy of the value returned by `locale()` |
| `iarray` | a null pointer |
| `parray` | a null pointer |
