# D.12.2.1 `strstreambuf` constructors [depr.strstreambuf.cons]

[Contents](README.md) · up: [D.12.2 Class `strstreambuf` \[depr.strstreambuf\]](depr.strstreambuf.md) · prev: [D.12.2 Class `strstreambuf` \[depr.strstreambuf\]](depr.strstreambuf.md) · next: [D.12.2.2 Member functions \[depr.strstreambuf.members\]](depr.strstreambuf.members.md)

```cpp
explicit strstreambuf(streamsize alsize_arg);
```

**1** *Effects:* Initializes the base class with `streambuf()`. The postconditions of this function are indicated in [Table 148](depr.strstreambuf.cons.md#tab:depr.strstreambuf.cons.sz).

<a id="tab:depr.strstreambuf.cons.sz"></a>
**Table 148: `strstreambuf(streamsize)` effects** [tab:depr.strstreambuf.cons.sz]

| Element | Value |
| --- | --- |
| `strmode` | `dynamic` |
| `alsize` | `alsize_arg` |
| `palloc` | a null pointer |
| `pfree` | a null pointer |

```cpp
strstreambuf(void* (*palloc_arg)(size_t), void (*pfree_arg)(void*));
```

**2** *Effects:* Initializes the base class with `streambuf()`. The postconditions of this function are indicated in [Table 149](depr.strstreambuf.cons.md#tab:depr.strstreambuf.cons.alloc).

<a id="tab:depr.strstreambuf.cons.alloc"></a>
**Table 149: `strstreambuf(void* (*)(size_t), void (*)(void*))` effects** [tab:depr.strstreambuf.cons.alloc]

| Element | Value |
| --- | --- |
| `strmode` | `dynamic` |
| `alsize` | an unspecified value |
| `palloc` | `palloc_arg` |
| `pfree` | `pfree_arg` |

```cpp
strstreambuf(char* gnext_arg, streamsize n, char* pbeg_arg = nullptr);
strstreambuf(signed char* gnext_arg, streamsize n,
             signed char* pbeg_arg = nullptr);
strstreambuf(unsigned char* gnext_arg, streamsize n,
             unsigned char* pbeg_arg = nullptr);
```

**3** *Effects:* Initializes the base class with `streambuf()`. The postconditions of this function are indicated in [Table 150](depr.strstreambuf.cons.md#tab:depr.strstreambuf.cons.ptr).

<a id="tab:depr.strstreambuf.cons.ptr"></a>
**Table 150: `strstreambuf(charT*, streamsize, charT*)` effects** [tab:depr.strstreambuf.cons.ptr]

| Element | Value |
| --- | --- |
| `strmode` | 0 |
| `alsize` | an unspecified value |
| `palloc` | a null pointer |
| `pfree` | a null pointer |

**4** `gnext_arg` shall point to the first element of an array object whose number of elements `N` is determined as follows:

- If `n > 0`, `N` is `n`.
- If `n == 0`, `N` is `std::strlen(gnext_arg)`.
- If `n < 0`, `N` is `INT_MAX`.[^1]

**5** If `pbeg_arg` is a null pointer, the function executes:

```cpp
setg(gnext_arg, gnext_arg, gnext_arg + N);
```

**6** Otherwise, the function executes:

```cpp
setg(gnext_arg, gnext_arg, pbeg_arg);
setp(pbeg_arg,  pbeg_arg + N);
```

```cpp
strstreambuf(const char* gnext_arg, streamsize n);
strstreambuf(const signed char* gnext_arg, streamsize n);
strstreambuf(const unsigned char* gnext_arg, streamsize n);
```

**7** *Effects:* Behaves the same as `strstreambuf((char*)gnext_arg,n)`, except that the constructor also sets `constant` in `strmode`.

```cpp
virtual ~strstreambuf();
```

**8** *Effects:* Destroys an object of class `strstreambuf`. The function frees the dynamically allocated array object only if `(strmode & allocated) != 0` and `(strmode & frozen) == 0`. ([[depr.strstreambuf.virtuals]](depr.strstreambuf.virtuals.md) describes how a dynamically allocated array object is freed.)

[^1]: The function signature `strlen(const char*)` is declared in `<cstring>` ([[cstring.syn]](lib/cstring.syn.md)). The macro `INT_MAX` is defined in `<climits>` ([[climits.syn]](lib/climits.syn.md)).
