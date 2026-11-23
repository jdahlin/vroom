# 29.6.3.4.1 Locales [streambuf.virt.locales]

[Contents](../../README.md) · up: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · prev: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · next: [29.6.3.4.2 Buffer management and positioning \[streambuf.virt.buffer\]](streambuf.virt.buffer.md)

```cpp
void imbue(const locale&);
```

**1** *Effects:* Change any translations based on locale.

**2** *Remarks:* Allows the derived class to be informed of changes in locale at the time they occur. Between invocations of this function a class derived from streambuf can safely cache results of calls to locale functions and to members of facets so obtained.

**3** *Default behavior:* Does nothing.
