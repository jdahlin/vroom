# 29.6.3.3.2 Get area access [streambuf.get.area]

[Contents](../README.md) · up: [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md) · prev: [29.6.3.3.1 Assignment \[streambuf.assign\]](streambuf.assign.md) · next: [29.6.3.3.3 Put area access \[streambuf.put.area\]](streambuf.put.area.md)

```cpp
char_type* eback() const;
```

**1** *Returns:* The beginning pointer for the input sequence.

```cpp
char_type* gptr() const;
```

**2** *Returns:* The next pointer for the input sequence.

```cpp
char_type* egptr() const;
```

**3** *Returns:* The end pointer for the input sequence.

```cpp
void gbump(int n);
```

**4** *Effects:* Adds `n` to the next pointer for the input sequence.

```cpp
void setg(char_type* gbeg, char_type* gnext, char_type* gend);
```

**5** *Postconditions:* `gbeg == eback()`, `gnext == gptr()`, and `gend == egptr()` are all `true`.
