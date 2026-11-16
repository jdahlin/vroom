# 29.5.3.2 State functions [fmtflags.state]

[Contents](../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.1.6 Class `ios_base::Init` \[ios.init\]](ios.init.md) · next: [29.5.3.3 Functions \[ios.base.locales\]](ios.base.locales.md)

```cpp
fmtflags flags() const;
```

**1** *Returns:* The format control information for both input and output.

```cpp
fmtflags flags(fmtflags fmtfl);
```

**2** *Postconditions:* `fmtfl == flags()`.

**3** *Returns:* The previous value of `flags()`.

```cpp
fmtflags setf(fmtflags fmtfl);
```

**4** *Effects:* Sets `fmtfl` in `flags()`.

**5** *Returns:* The previous value of `flags()`.

```cpp
fmtflags setf(fmtflags fmtfl, fmtflags mask);
```

**6** *Effects:* Clears `mask` in `flags()`, sets `fmtfl & mask` in `flags()`.

**7** *Returns:* The previous value of `flags()`.

```cpp
void unsetf(fmtflags mask);
```

**8** *Effects:* Clears `mask` in `flags()`.

```cpp
streamsize precision() const;
```

**9** *Returns:* The precision to generate on certain output conversions.

```cpp
streamsize precision(streamsize prec);
```

**10** *Postconditions:* `prec == precision()`.

**11** *Returns:* The previous value of `precision()`.

```cpp
streamsize width() const;
```

**12** *Returns:* The minimum field width (number of characters) to generate on certain output conversions.

```cpp
streamsize width(streamsize wide);
```

**13** *Postconditions:* `wide == width()`.

**14** *Returns:* The previous value of `width()`.
