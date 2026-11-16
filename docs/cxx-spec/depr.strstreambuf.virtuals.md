# D.12.2.3 `strstreambuf` overridden virtual functions [depr.strstreambuf.virtuals]

[Contents](README.md) · up: [D.12.2 Class `strstreambuf` \[depr.strstreambuf\]](depr.strstreambuf.md) · prev: [D.12.2.2 Member functions \[depr.strstreambuf.members\]](depr.strstreambuf.members.md) · next: [D.12.3 Class `istrstream` \[depr.istrstream\]](depr.istrstream.md)

```cpp
int_type overflow(int_type c = EOF) override;
```

**1** *Effects:* Appends the character designated by `c` to the output sequence, if possible, in one of two ways:

- If `c != EOF` and if either the output sequence has a write position available or the function makes a write position available (as described below), assigns `c` to `*pnext++`.

  Returns `(unsigned char)c`.

- If `c == EOF`, there is no character to append.

  Returns a value other than `EOF`.

**2** Returns `EOF` to indicate failure.

**3** *Remarks:* The function can alter the number of write positions available as a result of any call.

**4** To make a write position available, the function reallocates (or initially allocates) an array object with a sufficient number of elements `n` to hold the current array object (if any), plus at least one additional write position. How many additional write positions are made available is otherwise unspecified.[^1] If `palloc` is not a null pointer, the function calls `(*palloc)(n)` to allocate the new dynamic array object. Otherwise, it evaluates the expression `new charT[n]`. In either case, if the allocation fails, the function returns `EOF`. Otherwise, it sets `allocated` in `strmode`.

**5** To free a previously existing dynamic array object whose first element address is `p`: If `pfree` is not a null pointer, the function calls `(*pfree)(p)`. Otherwise, it evaluates the expression `delete[]p`.

**6** If `(strmode & dynamic) == 0`, or if `(strmode & frozen) != 0`, the function cannot extend the array (reallocate it with greater length) to make a write position available.

```cpp
int_type pbackfail(int_type c = EOF) override;
```

**7** Puts back the character designated by `c` to the input sequence, if possible, in one of three ways:

- If `c != EOF`, if the input sequence has a putback position available, and if `(char)c == gnext[-1]`, assigns `gnext - 1` to `gnext`.

  Returns `c`.

- If `c != EOF`, if the input sequence has a putback position available, and if `strmode & constant` is zero, assigns `c` to `*--gnext`.

  Returns `c`.

- If `c == EOF` and if the input sequence has a putback position available, assigns `gnext - 1` to `gnext`.

  Returns a value other than `EOF`.

**8** Returns `EOF` to indicate failure.

**9** *Remarks:* If the function can succeed in more than one of these ways, it is unspecified which way is chosen. The function can alter the number of putback positions available as a result of any call.

```cpp
int_type underflow() override;
```

**10** *Effects:* Reads a character from the *input sequence*, if possible, without moving the stream position past it, as follows:

- If the input sequence has a read position available, the function signals success by returning `(unsigned char)gnext`.

- Otherwise, if the current write next pointer `pnext` is not a null pointer and is greater than the current read end pointer `gend`, makes a *read position* available by assigning to `gend` a value greater than `gnext` and no greater than `pnext`.

  Returns `(unsigned char)*gnext`.

**11** Returns `EOF` to indicate failure.

**12** *Remarks:* The function can alter the number of read positions available as a result of any call.

```cpp
pos_type seekoff(off_type off, seekdir way, openmode which = in | out) override;
```

**13** *Effects:* Alters the stream position within one of the controlled sequences, if possible, as indicated in [Table 151](depr.strstreambuf.virtuals.md#tab:depr.strstreambuf.seekoff.pos).

<a id="tab:depr.strstreambuf.seekoff.pos"></a>
**Table 151: `seekoff` positioning** [tab:depr.strstreambuf.seekoff.pos]

| Conditions | Result |
| --- | --- |
| `(which & ios::in) != 0` | positions the input sequence |
| `(which & ios::out) != 0` | positions the output sequence |
| `(which & (ios::in \| ios::out)) ==`<br>`(ios::in \| ios::out)` and either<br>`way == ios::beg` or `way == ios::end` | positions both the input and the output sequences |
| Otherwise | the positioning operation fails. |

**14** For a sequence to be positioned, if its next pointer is a null pointer, the positioning operation fails. Otherwise, the function determines `newoff` as indicated in [Table 152](depr.strstreambuf.virtuals.md#tab:depr.strstreambuf.seekoff.newoff).

<a id="tab:depr.strstreambuf.seekoff.newoff"></a>
**Table 152: `newoff` values** [tab:depr.strstreambuf.seekoff.newoff]

| Condition | `newoff` Value |
| --- | --- |
| `way == ios::beg` | 0 |
| `way == ios::cur` | the next pointer minus the beginning pointer (`xnext - xbeg`). |
| `way == ios::end` | `seekhigh` minus the beginning pointer (`seekhigh - xbeg`). |

**15** If `(newoff + off) < (seeklow - xbeg)` or `(seekhigh - xbeg) < (newoff + off)`, the positioning operation fails. Otherwise, the function assigns `xbeg + newoff + off` to the next pointer `xnext`.

**16** *Returns:* `pos_type(newoff)`, constructed from the resultant offset `newoff` (of type `off_type`), that stores the resultant stream position, if possible. If the positioning operation fails, or if the constructed object cannot represent the resultant stream position, the return value is `pos_type(off_type(-1))`.

```cpp
pos_type seekpos(pos_type sp, ios_base::openmode which = ios_base::in | ios_base::out) override;
```

**17** *Effects:* Alters the stream position within one of the controlled sequences, if possible, to correspond to the stream position stored in `sp` (as described below).

- If `(which & ios::in) != 0`, positions the input sequence.
- If `(which & ios::out) != 0`, positions the output sequence.
- If the function positions neither sequence, the positioning operation fails.

**18** For a sequence to be positioned, if its next pointer is a null pointer, the positioning operation fails. Otherwise, the function determines `newoff` from `sp.offset()`:

- If `newoff` is an invalid stream position, has a negative value, or has a value greater than (`seekhigh - seeklow`), the positioning operation fails
- Otherwise, the function adds `newoff` to the beginning pointer `xbeg` and stores the result in the next pointer `xnext`.

**19** *Returns:* `pos_type(newoff)`, constructed from the resultant offset `newoff` (of type `off_type`), that stores the resultant stream position, if possible. If the positioning operation fails, or if the constructed object cannot represent the resultant stream position, the return value is `pos_type(off_type(-1))`.

```cpp
streambuf<char>* setbuf(char* s, streamsize n) override;
```

**20** *Effects:* Behavior is implementation-defined, except that `setbuf(0, 0)` has no effect.

[^1]: An implementation should consider `alsize` in making this decision.
