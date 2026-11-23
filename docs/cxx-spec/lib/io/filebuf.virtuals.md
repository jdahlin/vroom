# 29.9.2.4 Overridden virtual functions [filebuf.virtuals]

[Contents](../../README.md) · up: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md) · prev: [29.9.2.3 Member functions \[filebuf.members\]](filebuf.members.md) · next: [29.9.3 Class template `basic_ifstream` \[ifstream\]](ifstream.md)

```cpp
streamsize showmanyc() override;
```

**1** *Effects:* Behaves the same as `basic_streambuf::showmanyc()` ([[streambuf.virtuals]](streambuf.virtuals.md)).

**2** *Remarks:* An implementation might well provide an overriding definition for this function signature if it can determine that more characters can be read from the input sequence.

```cpp
int_type underflow() override;
```

**3** *Effects:* Behaves according to the description of `basic_streambuf<charT, traits>::underflow()`, with the specialization that a sequence of characters is read from the input sequence as if by reading from the associated file into an internal buffer (`extern_buf`) and then as if by doing:

```cpp
char   extern_buf[XSIZE];
char*  extern_end;
charT  intern_buf[ISIZE];
charT* intern_end;
codecvt_base::result r =
  a_codecvt.in(state, extern_buf, extern_buf+XSIZE, extern_end,
               intern_buf, intern_buf+ISIZE, intern_end);
```

This shall be done in such a way that the class can recover the position (`fpos_t`) corresponding to each character between `intern_buf` and `intern_end`. If the value of `r` indicates that `a_codecvt.in()` ran out of space in `intern_buf`, retry with a larger `intern_buf`.

```cpp
int_type uflow() override;
```

**4** *Effects:* Behaves according to the description of `basic_streambuf<charT, traits>::uflow()`, with the specialization that a sequence of characters is read from the input with the same method as used by `underflow`.

```cpp
int_type pbackfail(int_type c = traits::eof()) override;
```

**5** *Effects:* Puts back the character designated by `c` to the input sequence, if possible, in one of three ways:

- If `traits::eq_int_type(c, traits::eof())` returns `false` and if the function makes a putback position available and if `traits::eq(to_char_type(c), gptr()[-1])` returns `true`, decrements the next pointer for the input sequence, `gptr()`.

  Returns: `c`.

- If `traits::eq_int_type(c, traits::eof())` returns `false` and if the function makes a putback position available and if the function is permitted to assign to the putback position, decrements the next pointer for the input sequence, and stores `c` there.

  Returns: `c`.

- If `traits::eq_int_type(c, traits::eof())` returns `true`, and if either the input sequence has a putback position available or the function makes a putback position available, decrements the next pointer for the input sequence, `gptr()`.

  Returns: `traits::not_eof(c)`.

**6** *Returns:* As specified above, or `traits::eof()` to indicate failure.

**7** *Remarks:* If `is_open() == false`, the function always fails.

**8** The function does not put back a character directly to the input sequence.

**9** If the function can succeed in more than one of these ways, it is unspecified which way is chosen. The function can alter the number of putback positions available as a result of any call.

```cpp
int_type overflow(int_type c = traits::eof()) override;
```

**10** *Effects:* Behaves according to the description of `basic_streambuf<charT, traits>::overflow(c)`, except that the behavior of “consuming characters” is performed by first converting as if by:

```cpp
charT* b = pbase();
charT* p = pptr();
charT* end;
char   xbuf[XSIZE];
char*  xbuf_end;
codecvt_base::result r =
  a_codecvt.out(state, b, p, end, xbuf, xbuf+XSIZE, xbuf_end);
```

and then

- If `r == codecvt_base::error` then fail.
- If `r == codecvt_base::noconv` then output characters from `b` up to (and not including) `p`.
- If `r == codecvt_base::partial` then output to the file characters from `xbuf` up to `xbuf_end`, and repeat using characters from `end` to `p`. If output fails, fail (without repeating).
- Otherwise output from `xbuf` to `xbuf_end`, and fail if output fails. At this point if `b != p` and `b == end` (`xbuf` isn’t large enough) then increase `XSIZE` and repeat from the beginning.

**11** *Returns:* `traits::not_eof(c)` to indicate success, and `traits::eof()` to indicate failure. If `is_open() == false`, the function always fails.

```cpp
basic_streambuf* setbuf(char_type* s, streamsize n) override;
```

**12** *Effects:* If `setbuf(0, 0)` is called on a stream before any I/O has occurred on that stream, the stream becomes unbuffered. Otherwise the results are implementation-defined. “Unbuffered” means that `pbase()` and `pptr()` always return null and output to the file should appear as soon as possible.

```cpp
pos_type seekoff(off_type off, ios_base::seekdir way,
                 ios_base::openmode which
                   = ios_base::in | ios_base::out) override;
```

**13** *Effects:* Let `width` denote `a_codecvt.encoding()`. If `is_open() == false`, or `off != 0 && width <= 0`, then the positioning operation fails. Otherwise, if `way != basic_ios::cur` or `off != 0`, and if the last operation was output, then update the output sequence and write any unshift sequence. Next, seek to the new position: if `width > 0`, call `fseek(file, width * off, whence)`, otherwise call `fseek(file, 0, whence)`.

**14** *Remarks:* “The last operation was output” means either the last virtual operation was overflow or the put buffer is non-empty. “Write any unshift sequence” means, if `width` if less than zero then call `a_codecvt.unshift(state, xbuf, xbuf+XSIZE, xbuf_end)` and output the resulting unshift sequence. The function determines one of three values for the argument `whence`, of type `int`, as indicated in [Table 127](filebuf.virtuals.md#tab:filebuf.seekoff).

<a id="tab:filebuf.seekoff"></a>
**Table 127: `seekoff` effects** [tab:filebuf.seekoff]

| `way` Value | `stdio` Equivalent |
| --- | --- |
| `basic_ios::beg` | `SEEK_SET` |
| `basic_ios::cur` | `SEEK_CUR` |
| `basic_ios::end` | `SEEK_END` |

**15** *Returns:* A newly constructed `pos_type` object that stores the resultant stream position, if possible. If the positioning operation fails, or if the object cannot represent the resultant stream position, returns `pos_type(off_type(-1))`.

```cpp
pos_type seekpos(pos_type sp,
                 ios_base::openmode which
                   = ios_base::in | ios_base::out) override;
```

**16** Alters the file position, if possible, to correspond to the position stored in `sp` (as described below). Altering the file position performs as follows:

1. if `(om & ios_base::out) != 0`, then update the output sequence and write any unshift sequence;
2. set the file position to `sp` as if by a call to `fsetpos`;
3. if `(om & ios_base::in) != 0`, then update the input sequence;

where `om` is the open mode passed to the last call to `open()`. The operation fails if `is_open()` returns `false`.

**17** If `sp` is an invalid stream position, or if the function positions neither sequence, the positioning operation fails. If `sp` has not been obtained by a previous successful call to one of the positioning functions (`seekoff` or `seekpos`) on the same file the effects are undefined.

**18** *Returns:* `sp` on success. Otherwise returns `pos_type(off_type(-1))`.

```cpp
int sync() override;
```

**19** *Effects:* If a put area exists, calls `filebuf::overflow` to write the characters to the file, then flushes the file as if by calling `fflush(file)`. If a get area exists, the effect is implementation-defined.

```cpp
void imbue(const locale& loc) override;
```

**20** *Preconditions:* If the file is not positioned at its beginning and the encoding of the current locale as determined by `a_codecvt.encoding()` is state-dependent ([[locale.codecvt.virtuals]](../localization/locale.codecvt.virtuals.md)) then that facet is the same as the corresponding facet of `loc`.

**21** *Effects:* Causes characters inserted or extracted after this call to be converted according to `loc` until another call of `imbue`.

**22** *Remarks:* This may require reconversion of previously converted characters. This in turn may require the implementation to be able to reconstruct the original contents of the file.
