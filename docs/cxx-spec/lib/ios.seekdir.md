# 29.5.3.1.5 Type `ios_base::seekdir` [ios.seekdir]

[Contents](../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1.4 Type `ios_base::openmode` \[ios.openmode\]](ios.openmode.md) · next: [29.5.3.1.6 Class `ios_base::Init` \[ios.init\]](ios.init.md)

```cpp
using seekdir = T4;
```

**1** The type `seekdir` is an enumerated type ([[enumerated.types]](enumerated.types.md)) that contains the elements indicated in [Table 120](ios.seekdir.md#tab:ios.seekdir).

<a id="tab:ios.seekdir"></a>
**Table 120: `seekdir` effects** [tab:ios.seekdir]

| Element | Meaning |
| --- | --- |
| `beg` | request a seek (for subsequent input or output) relative to the beginning of the stream |
| `cur` | request a seek relative to the current position within the sequence |
| `end` | request a seek relative to the current end of the sequence |
