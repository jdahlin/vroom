# 21.2.1 Character traits requirements [char.traits.require]

[Contents](../README.md) · up: [21.2 Character traits \[char.traits\]](char.traits.md) · prev: [21.2 Character traits \[char.traits\]](char.traits.md) · next: [21.2.2 Traits typedefs \[char.traits.typedefs\]](char.traits.typedefs.md)

**1** In [Table 69](char.traits.require.md#tab:char.traits.req), `X` denotes a traits class defining types and functions for the character container type `C`; `c` and `d` denote values of type `C`; `p` and `q` denote values of type `const C*`; `s` denotes a value of type `C*`; `n`, `i` and `j` denote values of type `size_t`; `e` and `f` denote values of type `X::int_type`; `pos` denotes a value of type `X::pos_type`; and `r` denotes an lvalue of type `C`. Operations on `X` shall not throw exceptions.

<a id="tab:char.traits.req"></a>
**Table 69: Character traits requirements** [tab:char.traits.req]

| Expression | Return type | Assertion/note | Complexity |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `X::char_type` | `C` |  | compile-time |
| `X::int_type` |  | (described in [[char.traits.typedefs]](char.traits.typedefs.md)) | compile-time |
| `X::off_type` |  | (described in [[iostreams.limits.pos]](iostreams.limits.pos.md) and [[iostream.forward]](iostream.forward.md)) | compile-time |
| `X::pos_type` |  | (described in [[iostreams.limits.pos]](iostreams.limits.pos.md) and [[iostream.forward]](iostream.forward.md)) | compile-time |
| `X::state_type` |  | (described in [[char.traits.typedefs]](char.traits.typedefs.md)) | compile-time |
| `X::eq(c,d)` | `bool` | *Returns:* whether `c` is to be treated as equal to `d`. | constant |
| `X::lt(c,d)` | `bool` | *Returns:* whether `c` is to be treated as less than `d`. | constant |
| `X::compare(p,q,n)` | `int` | *Returns:* `0` if for each `i` in `[0,n)`, `X::eq(p[i],q[i])` is `true`; else, a negative value if, for some `j` in `[0,n)`, `X::lt(p[j],q[j])` is `true` and for each `i` in `[0,j)` `X::eq(p[i],q[i])` is `true`; else a positive value. | linear |
| `X::length(p)` | `size_t` | *Returns:* the smallest `i` such that `X::eq(p[i],charT())` is `true`. | linear |
| `X::find(p,n,c)` | `const X::char_type*` | *Returns:* the smallest `q` in `[p,p+n)` such that `X::eq(*q,c)` is `true`, zero otherwise. | linear |
| `X::move(s,p,n)` | `X::char_type*` | for each `i` in `[0,n)`, performs `X::assign(s[i],p[i])`. Copies correctly even where the ranges `[p,p+n)` and `[s,s+n)` overlap.<br>*Returns:* `s`. | linear |
| `X::copy(s,p,n)` | `X::char_type*` | *Preconditions:* `p` not in `[s,s+n)`. <br>*Returns:* `s`.<br>for each `i` in `[0,n)`, performs `X::assign(s[i],p[i])`. | linear |
| `X::assign(r,d)` | (not used) | assigns `r=d`. | constant |
| `X::assign(s,n,c)` | `X::char_type*` | for each `i` in `[0,n)`, performs `X::assign(s[i],c)`.<br>*Returns:* `s`. | linear |
| `X::not_eof(e)` | `int_type` | *Returns:* `e` if `X::eq_int_type(e,X::eof())` is `false`, otherwise a value `f` such that `X::eq_int_type(f,X::eof())` is `false`. | constant |
| `X::to_char_type(e)` | `X::char_type` | *Returns:* if for some `c`, `X::eq_int_type(e,X::to_int_type(c))` is `true`, `c`; else some unspecified value. | constant |
| `X::to_int_type(c)` | `X::int_type` | *Returns:* some value `e`, constrained by the definitions of `to_char_type` and `eq_int_type`. | constant |
| `X::eq_int_type(e,f)` | `bool` | *Returns:* for all `c` and `d`, `X::eq(c,d)` is equal to `X::eq_int_type(X::to_int_type(c), X::to_int_type(d))`; otherwise, yields `true` if `e` and `f` are both copies of `X::eof()`; otherwise, yields `false` if one of `e` and `f` is a copy of `X::eof()` and the other is not; otherwise the value is unspecified. | constant |
| `X::eof()` | `X::int_type` | *Returns:* a value `e` such that `X::eq_int_type(e,X::to_int_type(c))` is `false` for all values `c`. | constant |

**2** The class template

```cpp
template<class charT> struct char_traits;
```

is provided in the header `<string>` as a basis for explicit specializations.
