# 29.6.3.4.2 Buffer management and positioning [streambuf.virt.buffer]

[Contents](../README.md) · up: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md) · prev: [29.6.3.4.1 Locales \[streambuf.virt.locales\]](streambuf.virt.locales.md) · next: [29.6.3.4.3 Get area \[streambuf.virt.get\]](streambuf.virt.get.md)

```cpp
basic_streambuf* setbuf(char_type* s, streamsize n);
```

**1** *Effects:* Influences stream buffering in a way that is defined separately for each class derived from `basic_streambuf` in this Clause ([[stringbuf.virtuals]](stringbuf.virtuals.md), [[filebuf.virtuals]](filebuf.virtuals.md)).

**2** *Default behavior:* Does nothing. Returns `this`.

```cpp
pos_type seekoff(off_type off, ios_base::seekdir way,
                 ios_base::openmode which
                  = ios_base::in | ios_base::out);
```

**3** *Effects:* Alters the stream positions within one or more of the controlled sequences in a way that is defined separately for each class derived from `basic_streambuf` in this Clause ([[stringbuf.virtuals]](stringbuf.virtuals.md), [[filebuf.virtuals]](filebuf.virtuals.md)).

**4** *Default behavior:* Returns `pos_type(off_type(-1))`.

```cpp
pos_type seekpos(pos_type sp,
                 ios_base::openmode which
                  = ios_base::in | ios_base::out);
```

**5** *Effects:* Alters the stream positions within one or more of the controlled sequences in a way that is defined separately for each class derived from `basic_streambuf` in this Clause ([[stringbuf]](stringbuf.md), [[filebuf]](filebuf.md)).

**6** *Default behavior:* Returns `pos_type(off_type(-1))`.

```cpp
int sync();
```

**7** *Effects:* Synchronizes the controlled sequences with the arrays. That is, if `pbase()` is non-null the characters between `pbase()` and `pptr()` are written to the controlled sequence. The pointers may then be reset as appropriate.

**8** *Returns:* `-1` on failure. What constitutes failure is determined by each derived class ([[filebuf.virtuals]](filebuf.virtuals.md)).

**9** *Default behavior:* Returns zero.
