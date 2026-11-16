# 29.6.3.4.3 Get area [streambuf.virt.get]

[Contents](../README.md) · up: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · prev: [29.6.3.4.2 Buffer management and positioning \[streambuf.virt.buffer\]](streambuf.virt.buffer.md) · next: [29.6.3.4.4 Putback \[streambuf.virt.pback\]](streambuf.virt.pback.md)

```cpp
streamsize showmanyc();@\footnote{\textrm{The morphemes of \tcode{showmanyc}\
are ``es-how-many-see'', not ``show-manic''.}}@
```

**1** *Returns:* An estimate of the number of characters available in the sequence, or -1. If it returns a positive value, then successive calls to `underflow()` will not return `traits::eof()` until at least that number of characters have been extracted from the stream. If `showmanyc()` returns -1, then calls to `underflow()` or `uflow()` will fail.[^1]

**2** *Default behavior:* Returns zero.

**3** *Remarks:* Uses `traits::eof()`.

```cpp
streamsize xsgetn(char_type* s, streamsize n);
```

**4** *Effects:* Assigns up to `n` characters to successive elements of the array whose first element is designated by `s`. The characters assigned are read from the input sequence as if by repeated calls to `sbumpc()`. Assigning stops when either `n` characters have been assigned or a call to `sbumpc()` would return `traits::eof()`.

**5** *Returns:* The number of characters assigned.[^2]

**6** *Remarks:* Uses `traits::eof()`.

```cpp
int_type underflow();
```

**7** *Remarks:* The public members of `basic_streambuf` call this virtual function only if `gptr()` is null or `gptr() >= egptr()`.

**8** *Returns:* `traits::to_int_type(c)`, where `c` is the first *character* of the *pending sequence*, without moving the input sequence position past it. If the pending sequence is null then the function returns `traits::eof()` to indicate failure.

**9** The *pending sequence* of characters is defined as the concatenation of

- the empty sequence if `gptr()` is null, otherwise the characters in `[gptr(), egptr())`, followed by
- some (possibly empty) sequence of characters read from the input sequence.

**10** The *result character* is the first character of the pending sequence if it is non-empty, otherwise the next character that would be read from the input sequence.

**11** The *backup sequence* is the empty sequence if `eback()` is null, otherwise the characters in `[eback(), gptr())`.

**12** *Effects:* The function sets up the `gptr()` and `egptr()` such that if the pending sequence is non-empty, then `egptr()` is non-null and the characters in `[gptr(), egptr())` are the characters in the pending sequence, otherwise either `gptr()` is null or `gptr() == egptr()`.

**13** If `eback()` and `gptr()` are non-null then the function is not constrained as to their contents, but the “usual backup condition” is that either

- the backup sequence contains at least `gptr() - eback()` characters, in which case the characters in `[eback(), gptr())` agree with the last `gptr() - eback()` characters of the backup sequence, or
- the characters in `[gptr() - n, gptr())` agree with the backup sequence (where `n` is the length of the backup sequence).

**14** *Default behavior:* Returns `traits::eof()`.

```cpp
int_type uflow();
```

**15** *Preconditions:* The constraints are the same as for `underflow()`, except that the result character is transferred from the pending sequence to the backup sequence, and the pending sequence is not empty before the transfer.

**16** *Default behavior:* Calls `underflow()`. If `underflow()` returns `traits::eof()`, returns `traits::eof()`. Otherwise, returns the value of `traits::to_int_type(*gptr())` and increment the value of the next pointer for the input sequence.

**17** *Returns:* `traits::eof()` to indicate failure.

[^1]: `underflow` or `uflow` might fail by throwing an exception prematurely. The intention is not only that the calls will not return `eof()` but that they will return “immediately”.
[^2]: Classes derived from `basic_streambuf` can provide more efficient ways to implement `xsgetn()` and `xsputn()` by overriding these definitions from the base class.
