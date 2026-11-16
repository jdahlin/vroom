# 29.6.3.3.3 Put area access [streambuf.put.area]

[Contents](../README.md) · up: [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md) · prev: [29.6.3.3.2 Get area access \[streambuf.get.area\]](streambuf.get.area.md) · next: [29.6.3.4 Virtual functions \[streambuf.virtuals\]](streambuf.virtuals.md)

```cpp
char_type* pbase() const;
```

**1** *Returns:* The beginning pointer for the output sequence.

```cpp
char_type* pptr() const;
```

**2** *Returns:* The next pointer for the output sequence.

```cpp
char_type* epptr() const;
```

**3** *Returns:* The end pointer for the output sequence.

```cpp
void pbump(int n);
```

**4** *Effects:* Adds `n` to the next pointer for the output sequence.

```cpp
void setp(char_type* pbeg, char_type* pend);
```

**5** *Postconditions:* `pbeg == pbase()`, `pbeg == pptr()`, and `pend == epptr()` are all `true`.
