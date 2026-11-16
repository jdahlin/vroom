# 29.5.3.1.3 Type `ios_base::iostate` [ios.iostate]

[Contents](../../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1.2 Type `ios_base::fmtflags` \[ios.fmtflags\]](ios.fmtflags.md) · next: [29.5.3.1.4 Type `ios_base::openmode` \[ios.openmode\]](ios.openmode.md)

```cpp
using iostate = T2;
```

**1** The type `iostate` is a bitmask type ([[bitmask.types]](../library/bitmask.types.md)) that contains the elements indicated in [Table 118](ios.iostate.md#tab:ios.iostate).

<a id="tab:ios.iostate"></a>
**Table 118: `iostate` effects** [tab:ios.iostate]

| Element | Effect(s) if set |
| --- | --- |
| `badbit` | indicates a loss of integrity in an input or output sequence (such as an irrecoverable read error from a file); |
| `eofbit` | indicates that an input operation reached the end of an input sequence; |
| `failbit` | indicates that an input operation failed to read the expected characters, or that an output operation failed to generate the desired characters. |

**2** Type `iostate` also defines the constant:

- `goodbit`, the value zero.
