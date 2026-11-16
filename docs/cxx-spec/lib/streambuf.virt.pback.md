# 29.6.3.4.4 Putback [streambuf.virt.pback]

[Contents](../README.md) · up: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · prev: [29.6.3.4.3 Get area \[streambuf.virt.get\]](streambuf.virt.get.md) · next: [29.6.3.4.5 Put area \[streambuf.virt.put\]](streambuf.virt.put.md)

```cpp
int_type pbackfail(int_type c = traits::eof());
```

**1** *Remarks:* The public functions of `basic_streambuf` call this virtual function only when `gptr()` is null, `gptr() == eback()`, or `traits::eq(traits::to_char_type(c), gptr()[-1])` returns `false`. Other calls shall also satisfy that constraint.

The *pending sequence* is defined as for `underflow()`, with the modifications that

- If `traits::eq_int_type(c, traits::eof())` returns `true`, then the input sequence is backed up one character before the pending sequence is determined.
- If `traits::eq_int_type(c, traits::eof())` returns `false`, then `c` is prepended. Whether the input sequence is backed up or modified in any other way is unspecified.

**2** *Postconditions:* On return, the constraints of `gptr()`, `eback()`, and `pptr()` are the same as for `underflow()`.

**3** *Returns:* `traits::eof()` to indicate failure. Failure may occur because the input sequence could not be backed up, or if for some other reason the pointers could not be set consistent with the constraints. `pbackfail()` is called only when put back has really failed.

**4** Returns some value other than `traits::eof()` to indicate success.

**5** *Default behavior:* Returns `traits::eof()`.
