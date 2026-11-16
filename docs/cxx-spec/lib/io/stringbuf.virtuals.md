# 29.8.2.4 Overridden virtual functions [stringbuf.virtuals]

[Contents](../../README.md) · up: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md) · prev: [29.8.2.3 Member functions \[stringbuf.members\]](stringbuf.members.md) · next: [29.8.3 Class template `basic_istringstream` \[istringstream\]](istringstream.md)

```cpp
int_type underflow() override;
```

**1** *Returns:* If the input sequence has a read position available, returns `traits::to_int_type(*gptr())`. Otherwise, returns `traits::eof()`. Any character in the underlying buffer which has been initialized is considered to be part of the input sequence.

```cpp
int_type pbackfail(int_type c = traits::eof()) override;
```

**2** *Effects:* Puts back the character designated by `c` to the input sequence, if possible, in one of three ways:

- If `traits::eq_int_type(c, traits::eof())` returns `false` and if the input sequence has a putback position available, and if `traits::eq(to_char_type(c), gptr()[-1])` returns `true`, assigns `gptr() - 1` to `gptr()`.

  Returns: `c`.

- If `traits::eq_int_type(c, traits::eof())` returns `false` and if the input sequence has a putback position available, and if `mode` `&` `ios_base::out` is nonzero, assigns `c` to `*--gptr()`.

  Returns: `c`.

- If `traits::eq_int_type(c, traits::eof())` returns `true` and if the input sequence has a putback position available, assigns `gptr() - 1` to `gptr()`.

  Returns: `traits::not_eof(c)`.

**3** *Returns:* As specified above, or `traits::eof()` to indicate failure.

**4** *Remarks:* If the function can succeed in more than one of these ways, it is unspecified which way is chosen.

```cpp
int_type overflow(int_type c = traits::eof()) override;
```

**5** *Effects:* Appends the character designated by `c` to the output sequence, if possible, in one of two ways:

- If `traits::eq_int_type(c, traits::eof())` returns `false` and if either the output sequence has a write position available or the function makes a write position available (as described below), the function calls `sputc(c)`.

  Signals success by returning `c`.

- If `traits::eq_int_type(c, traits::eof())` returns `true`, there is no character to append.

  Signals success by returning a value other than `traits::eof()`.

**6** *Remarks:* The function can alter the number of write positions available as a result of any call.

**7** *Returns:* As specified above, or `traits::eof()` to indicate failure.

**8** The function can make a write position available only if `ios_base::out` is set in `mode`. To make a write position available, the function reallocates (or initially allocates) an array object with a sufficient number of elements to hold the current array object (if any), plus at least one additional write position. If `ios_base::in` is set in `mode`, the function alters the read end pointer `egptr()` to point just past the new write position.

```cpp
pos_type seekoff(off_type off, ios_base::seekdir way,
                 ios_base::openmode which
                   = ios_base::in | ios_base::out) override;
```

**9** *Effects:* Alters the stream position within one of the controlled sequences, if possible, as indicated in [Table 124](stringbuf.virtuals.md#tab:stringbuf.seekoff.pos).

<a id="tab:stringbuf.seekoff.pos"></a>
**Table 124: `seekoff` positioning** [tab:stringbuf.seekoff.pos]

| Conditions | Result |
| --- | --- |
| `ios_base::in` is set in `which` | positions the input sequence |
| `ios_base::out` is set in `which` | positions the output sequence |
| both `ios_base::in` and `ios_base::out` are set in `which` and either<br>`way == ios_base::beg` or<br>`way == ios_base::end` | positions both the input and the output sequences |
| Otherwise | the positioning operation fails. |

**10** For a sequence to be positioned, the function determines `newoff` as indicated in [Table 125](stringbuf.virtuals.md#tab:stringbuf.seekoff.newoff). If the sequence’s next pointer (either `gptr()` or `pptr()`) is a null pointer and `newoff` is nonzero, the positioning operation fails.

<a id="tab:stringbuf.seekoff.newoff"></a>
**Table 125: `newoff` values** [tab:stringbuf.seekoff.newoff]

| Condition | `newoff` Value |
| --- | --- |
| `way == ios_base::beg` | 0 |
| `way == ios_base::cur` | the next pointer minus the beginning pointer (`xnext - xbeg`). |
| `way == ios_base::end` | the high mark pointer minus the beginning pointer (`high_mark - xbeg`). |

**11** If `(newoff + off) < 0`, or if `newoff + off` refers to an uninitialized character ([[stringbuf.members]](stringbuf.members.md)), the positioning operation fails. Otherwise, the function assigns `xbeg + newoff + off` to the next pointer `xnext`.

**12** *Returns:* `pos_type(newoff)`, constructed from the resultant offset `newoff` (of type `off_type`), that stores the resultant stream position, if possible. If the positioning operation fails, or if the constructed object cannot represent the resultant stream position, the return value is `pos_type(off_type(-1))`.

```cpp
pos_type seekpos(pos_type sp,
                 ios_base::openmode which
                   = ios_base::in | ios_base::out) override;
```

**13** *Effects:* Equivalent to `seekoff(off_type(sp), ios_base::beg, which)`.

**14** *Returns:* `sp` to indicate success, or `pos_type(off_type(-1))` to indicate failure.

```cpp
basic_streambuf<charT, traits>* setbuf(charT* s, streamsize n);
```

**15** *Effects:* implementation-defined, except that `setbuf(0, 0)` has no effect.

**16** *Returns:* `this`.
