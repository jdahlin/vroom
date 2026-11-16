# 29.5.3.4 Static members [ios.members.static]

[Contents](../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.3 Functions \[ios.base.locales\]](ios.base.locales.md) · next: [29.5.3.5 Storage functions \[ios.base.storage\]](ios.base.storage.md)

```cpp
static bool sync_with_stdio(bool sync = true);
```

**1** *Returns:* `true` if the previous state of the standard iostream objects ([[iostream.objects]](iostream.objects.md)) was synchronized and otherwise returns `false`. The first time it is called, the function returns `true`.

**2** *Effects:* If any input or output operation has occurred using the standard streams prior to the call, the effect is implementation-defined. Otherwise, called with a `false` argument, it allows the standard streams to operate independently of the standard C streams.

**3** When a standard iostream object `str` is *synchronized* with a standard stdio stream `f`, the effect of inserting a character `c` by

```cpp
fputc(f, c);
```

is the same as the effect of

```cpp
str.rdbuf()->sputc(c);
```

for any sequences of characters; the effect of extracting a character `c` by

```cpp
c = fgetc(f);
```

is the same as the effect of

```cpp
c = str.rdbuf()->sbumpc();
```

for any sequences of characters; and the effect of pushing back a character `c` by

```cpp
ungetc(c, f);
```

is the same as the effect of

```cpp
str.rdbuf()->sputbackc(c);
```

for any sequence of characters.[^1]

[^1]: This implies that operations on a standard iostream object can be mixed arbitrarily with operations on the corresponding stdio stream. In practical terms, synchronization usually means that a standard iostream object and a standard stdio object share a buffer.
