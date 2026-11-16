# 29.5.5.3 Member functions [basic.ios.members]

[Contents](../README.md) · up: [29.5.5 Class template `basic_ios` \[ios\]](ios.md) · prev: [29.5.5.2 Constructors \[basic.ios.cons\]](basic.ios.cons.md) · next: [29.5.5.4 Flags functions \[iostate.flags\]](iostate.flags.md)

```cpp
basic_ostream<charT, traits>* tie() const;
```

**1** *Returns:* An output sequence that is *tied* to (synchronized with) the sequence controlled by the stream buffer.

```cpp
basic_ostream<charT, traits>* tie(basic_ostream<charT, traits>* tiestr);
```

**2** *Preconditions:* If `tiestr` is not null, `tiestr` is not reachable by traversing the linked list of tied stream objects starting from `tiestr->tie()`.

**3** *Postconditions:* `tiestr == tie()`.

**4** *Returns:* The previous value of `tie()`.

```cpp
basic_streambuf<charT, traits>* rdbuf() const;
```

**5** *Returns:* A pointer to the `streambuf` associated with the stream.

```cpp
basic_streambuf<charT, traits>* rdbuf(basic_streambuf<charT, traits>* sb);
```

**6** *Effects:* Calls `clear()`.

**7** *Postconditions:* `sb == rdbuf()`.

**8** *Returns:* The previous value of `rdbuf()`.

```cpp
locale imbue(const locale& loc);
```

**9** *Effects:* Calls `ios_base::imbue(loc)` ([[ios.base.locales]](ios.base.locales.md)) and if `rdbuf() != 0` then `rdbuf()->pubimbue(loc)` ([[streambuf.locales]](streambuf.locales.md)).

**10** *Returns:* The prior value of `ios_base::imbue()`.

```cpp
char narrow(char_type c, char dfault) const;
```

**11** *Returns:* `use_facet<ctype<char_type>>(getloc()).narrow(c, dfault)`

```cpp
char_type widen(char c) const;
```

**12** *Returns:* `use_facet<ctype<char_type>>(getloc()).widen(c)`

```cpp
char_type fill() const;
```

**13** *Returns:* The character used to pad (fill) an output conversion to the specified field width.

```cpp
char_type fill(char_type fillch);
```

**14** *Postconditions:* `traits::eq(fillch, fill())`.

**15** *Returns:* The previous value of `fill()`.

```cpp
basic_ios& copyfmt(const basic_ios& rhs);
```

**16** *Effects:* If `(this == addressof(rhs))` is `true` does nothing. Otherwise assigns to the member objects of `*this` the corresponding member objects of `rhs` as follows:

- calls each registered callback pair `(fn, idx)` as `(*fn)(erase_event, *this, idx)`;

- then, assigns to the member objects of `*this` the corresponding member objects of `rhs`, except that

  - `rdstate()`, `rdbuf()`, and `exceptions()` are left unchanged;
  - the contents of arrays pointed at by `pword` and `iword` are copied, not the pointers themselves;[^1] and
  - if any newly stored pointer values in `*this` point at objects stored outside the object `rhs` and those objects are destroyed when `rhs` is destroyed, the newly stored pointer values are altered to point at newly constructed copies of the objects;

- then, calls each callback pair that was copied from `rhs` as `(*fn)(copyfmt_event, *this, idx)`;

- then, calls `exceptions(rhs.exceptions())`.

**17** [*Note 1*: The second pass through the callback pairs permits a copied `pword` value to be zeroed, or to have its referent deep copied or reference counted, or to have other special action taken. — *end note*]

**18** *Postconditions:* The postconditions of this function are indicated in [Table 123](basic.ios.members.md#tab:basic.ios.copyfmt).

<a id="tab:basic.ios.copyfmt"></a>
**Table 123: `basic_ios::copyfmt()` effects** [tab:basic.ios.copyfmt]

| Element | Value |
| --- | --- |
| `rdbuf()` | *unchanged* |
| `tie()` | `rhs.tie()` |
| `rdstate()` | *unchanged* |
| `exceptions()` | `rhs.exceptions()` |
| `flags()` | `rhs.flags()` |
| `width()` | `rhs.width()` |
| `precision()` | `rhs.precision()` |
| `fill()` | `rhs.fill()` |
| `getloc()` | `rhs.getloc()` |

**19** *Returns:* `*this`.

```cpp
void move(basic_ios& rhs);
void move(basic_ios&& rhs);
```

**20** *Postconditions:* `*this` has the state that `rhs` had before the function call, except that `rdbuf()` returns `nullptr`. `rhs` is in a valid but unspecified state, except that `rhs.rdbuf()` returns the same value as it returned before the function call, and `rhs.tie()` returns `nullptr`.

```cpp
void swap(basic_ios& rhs) noexcept;
```

**21** *Effects:* The states of `*this` and `rhs` are exchanged, except that `rdbuf()` returns the same value as it returned before the function call, and `rhs.rdbuf()` returns the same value as it returned before the function call.

```cpp
void set_rdbuf(basic_streambuf<charT, traits>* sb);
```

**22** *Preconditions:* `sb != nullptr` is `true`.

**23** *Effects:* Associates the `basic_streambuf` object pointed to by `sb` with this stream without calling `clear()`.

**24** *Postconditions:* `rdbuf() == sb` is `true`.

**25** *Throws:* Nothing.

[^1]: This suggests an infinite amount of copying, but the implementation can keep track of the maximum element of the arrays that is nonzero.
