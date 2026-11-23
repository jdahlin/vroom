# 29.5.4.2 Requirements [fpos.operations]

[Contents](../../README.md) · up: [29.5.4 Class template `fpos` \[fpos\]](fpos.md) · prev: [29.5.4.1 Members \[fpos.members\]](fpos.members.md) · next: [29.5.5 Class template `basic_ios` \[ios\]](ios.md)

**1** An `fpos` type specifies file position information. It holds a state object whose type is equal to the template parameter `stateT`. Type `stateT` shall meet the *Cpp17DefaultConstructible* ([Table 27](../library/utility.arg.requirements.md#tab:cpp17.defaultconstructible)), *Cpp17CopyConstructible* ([Table 29](../library/utility.arg.requirements.md#tab:cpp17.copyconstructible)), *Cpp17CopyAssignable* ([Table 31](../library/utility.arg.requirements.md#tab:cpp17.copyassignable)), and *Cpp17Destructible* ([Table 32](../library/utility.arg.requirements.md#tab:cpp17.destructible)) requirements. If `is_trivially_copy_constructible_v<stateT>` is `true`, then `fpos<stateT>` has a trivial copy constructor. If `is_trivially_copy_assignable<stateT>` is `true`, then `fpos<stateT>` has a trivial copy assignment operator. If `is_trivially_destructible_v<stateT>` is `true`, then `fpos<stateT>` has a trivial destructor. All specializations of `fpos` meet the *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, *Cpp17Destructible*, and *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements. In addition, the expressions shown in [Table 121](fpos.operations.md#tab:fpos.operations) are valid and have the indicated semantics. In that table,

- `P` refers to an instance of `fpos`,
- `p` and `q` refer to values of type `P` or `const P`,
- `pl` and `ql` refer to modifiable lvalues of type `P`,
- `O` refers to type `streamoff`, and
- `o` refers to a value of type `streamoff` or `const streamoff`.

<a id="tab:fpos.operations"></a>
**Table 121: Position type requirements** [tab:fpos.operations]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `P(o)` | `P` | converts from `offset` | *Effects:* Value-initializes the state object. |
| `P p(o);`<br>`P p = o;` |  |  | *Effects:* Value-initializes the state object. <br>*Postconditions:* `p == P(o)` |
| `P()` | `P` | `P(0)` |  |
| `P p;` |  | `P p(0);` |  |
| `O(p)` | `streamoff` | converts to `offset` | `P(O(p)) == p` |
| `p != q` | convertible to `bool` | `!(p == q)` |  |
| `p + o` | `P` | `+` offset | *Remarks:* With `ql = p + o;`, then: `ql - o == p` |
| `pl += o` | `P&` | `+=` offset | *Remarks:* With `ql = pl;` before the `+=`, then: `pl - o == ql` |
| `p - o` | `P` | `-` offset | *Remarks:* With `ql = p - o;`, then: `ql + o == p` |
| `pl -= o` | `P&` | `-=` offset | *Remarks:* With `ql = pl;` before the `-=`, then: `pl + o == ql` |
| `o + p` | convertible to `P` | `p + o` | `P(o + p) == p + o` |
| `p - q` | `streamoff` | distance | `p == q + (p - q)` |

**2** Stream operations that return a value of type `traits::pos_type` return `P(O(-1))` as an invalid value to signal an error. If this value is used as an argument to any `istream`, `ostream`, or `streambuf` member that accepts a value of type `traits::pos_type` then the behavior of that function is undefined.
