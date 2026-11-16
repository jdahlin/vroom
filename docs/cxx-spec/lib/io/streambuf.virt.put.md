# 29.6.3.4.5 Put area [streambuf.virt.put]

[Contents](../../README.md) · up: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · prev: [29.6.3.4.4 Putback \[streambuf.virt.pback\]](streambuf.virt.pback.md) · next: [29.7 Formatting and manipulators \[iostream.format\]](iostream.format.md)

```cpp
streamsize xsputn(const char_type* s, streamsize n);
```

**1** *Effects:* Writes up to `n` characters to the output sequence as if by repeated calls to `sputc(c)`. The characters written are obtained from successive elements of the array whose first element is designated by `s`. Writing stops when either `n` characters have been written or a call to `sputc(c)` would return `traits::eof()`. It is unspecified whether the function calls `overflow()` when `pptr() == epptr()` becomes `true` or whether it achieves the same effects by other means.

**2** *Returns:* The number of characters written.

```cpp
int_type overflow(int_type c = traits::eof());
```

**3** *Effects:* Consumes some initial subsequence of the characters of the *pending sequence*. The pending sequence is defined as the concatenation of

- the empty sequence if `pbase()` is null, otherwise the `pptr() - pbase()` characters beginning at `pbase()`, followed by
- the empty sequence if `traits::eq_int_type(c, traits::eof())` returns `true`, otherwise the sequence consisting of `c`.

**4** *Remarks:* The member functions `sputc()` and `sputn()` call this function in case that no room can be found in the put buffer enough to accommodate the argument character sequence.

**5** *Preconditions:* Every overriding definition of this virtual function obeys the following constraints:

- The effect of consuming a character on the associated output sequence is specified.[^1]
- Let `r` be the number of characters in the pending sequence not consumed. If `r` is nonzero then `pbase()` and `pptr()` are set so that: `pptr() - pbase() == r` and the `r` characters starting at `pbase()` are the associated output stream. In case `r` is zero (all characters of the pending sequence have been consumed) then either `pbase()` is set to `nullptr`, or `pbase()` and `pptr()` are both set to the same non-null value.
- The function may fail if either appending some character to the associated output stream fails or if it is unable to establish `pbase()` and `pptr()` according to the above rules.

**6** *Returns:* `traits::eof()` or throws an exception if the function fails.

Otherwise, returns some value other than `traits::eof()` to indicate success.[^2]

**7** *Default behavior:* Returns `traits::eof()`.

[^1]: That is, for each class derived from an instance of `basic_streambuf` in this Clause ([[stringbuf]](stringbuf.md), [[filebuf]](filebuf.md)), a specification of how consuming a character effects the associated output sequence is given. There is no requirement on a program-defined class.
[^2]: Typically, `overflow` returns `c` to indicate success, except when `traits::eq_int_type(c, traits::eof())` returns `true`, in which case it returns `traits::not_eof(c)`.
