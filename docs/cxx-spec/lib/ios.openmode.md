# 29.5.3.1.4 Type `ios_base::openmode` [ios.openmode]

[Contents](../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1.3 Type `ios_base::iostate` \[ios.iostate\]](ios.iostate.md) · next: [29.5.3.1.5 Type `ios_base::seekdir` \[ios.seekdir\]](ios.seekdir.md)

```cpp
using openmode = T3;
```

**1** The type `openmode` is a bitmask type ([[bitmask.types]](bitmask.types.md)). It contains the elements indicated in [Table 119](ios.openmode.md#tab:ios.openmode).

<a id="tab:ios.openmode"></a>
**Table 119: `openmode` effects** [tab:ios.openmode]

| Element | Effect(s) if set |
| --- | --- |
| `app` | seek to end before each write |
| `ate` | open and seek to end immediately after opening |
| `binary` | perform input and output in binary mode (as opposed to text mode) |
| `in` | open for input |
| `out` | open for output |
| `trunc` | truncate an existing stream when opening |
