# 28.4.1.4.2 Virtual functions [locale.codecvt.virtuals]

[Contents](../../README.md) · up: [28.4.1.4 Class template `codecvt` \[locale.codecvt\]](locale.codecvt.md) · prev: [28.4.1.4.1 Members \[locale.codecvt.members\]](locale.codecvt.members.md) · next: [28.4.1.5 Class template `codecvt_byname` \[locale.codecvt.byname\]](locale.codecvt.byname.md)

```cpp
result do_out(
    stateT& state,
    const internT* from, const internT* from_end, const internT*& from_next,
    externT* to, externT* to_end, externT*& to_next) const;

result do_in(
    stateT& state,
    const externT* from, const externT* from_end, const externT*& from_next,
    internT* to, internT* to_end, internT*& to_next) const;
```

**1** *Preconditions:* `(from <= from_end && to <= to_end)` is well-defined and `true`; `state` is initialized, if at the beginning of a sequence, or else is equal to the result of converting the preceding characters in the sequence.

**2** *Effects:* Translates characters in the source range `[from, from_end)`, placing the results in sequential positions starting at destination `to`. Converts no more than `(from_end - from)` source elements, and stores no more than `(to_end - to)` destination elements.

Stops if it encounters a character it cannot convert. It always leaves the `from_next` and `to_next` pointers pointing one beyond the last element successfully converted. If returns `noconv`, `internT` and `externT` are the same type and the converted sequence is identical to the input sequence `[from, from_next)`. `to_next` is set equal to `to`, the value of `state` is unchanged, and there are no changes to the values in `[to, to_end)`.

**3** A `codecvt` facet that is used by `basic_filebuf` ([[file.streams]](../io/file.streams.md)) shall have the property that if

```cpp
do_out(state, from, from_end, from_next, to, to_end, to_next)
```

would return `ok`, where `from != from_end`, then

```cpp
do_out(state, from, from + 1, from_next, to, to_end, to_next)
```

shall also return `ok`, and that if

```cpp
do_in(state, from, from_end, from_next, to, to_end, to_next)
```

would return `ok`, where `to != to_end`, then

```cpp
do_in(state, from, from_end, from_next, to, to + 1, to_next)
```

shall also return `ok`.[^1] [*Note 1*: As a result of operations on `state`, it can return `ok` or `partial` and set `from_next == from` and `to_next != to`. — *end note*]

**4** *Remarks:* Its operations on `state` are unspecified. [*Note 2*: This argument can be used, for example, to maintain shift state, to specify conversion options (such as count only), or to identify a cache of seek offsets. — *end note*]

**5** *Returns:* An enumeration value, as summarized in [Table 104](locale.codecvt.virtuals.md#tab:locale.codecvt.inout).

<a id="tab:locale.codecvt.inout"></a>
**Table 104: `do_in/do_out` result values** [tab:locale.codecvt.inout]

| Value | Meaning |
| --- | --- |
| `ok` | completed the conversion |
| `partial` | not all source characters converted |
| `error` | encountered a character in `[from, from_end)` that it could not convert |
| `noconv` | `internT` and `externT` are the same type, and input sequence is identical to converted sequence |

A return value of `partial`, if `(from_next == from_end)`, indicates that either the destination sequence has not absorbed all the available destination elements, or that additional source elements are needed before another destination element can be produced.

```cpp
result do_unshift(stateT& state, externT* to, externT* to_end, externT*& to_next) const;
```

**6** *Preconditions:* `(to <= to_end)` is well-defined and `true`; `state` is initialized, if at the beginning of a sequence, or else is equal to the result of converting the preceding characters in the sequence.

**7** *Effects:* Places characters starting at `to` that should be appended to terminate a sequence when the current `stateT` is given by `state`.[^2] Stores no more than `(to_end - to)` destination elements, and leaves the `to_next` pointer pointing one beyond the last element successfully stored.

**8** *Returns:* An enumeration value, as summarized in [Table 105](locale.codecvt.virtuals.md#tab:locale.codecvt.unshift).

<a id="tab:locale.codecvt.unshift"></a>
**Table 105: `do_unshift` result values** [tab:locale.codecvt.unshift]

| Value | Meaning |
| --- | --- |
| `ok` | completed the sequence |
| `partial` | space for more than `to_end - to` destination elements was needed to terminate a sequence given the value of `state` |
| `error` | an unspecified error has occurred |
| `noconv` | no termination is needed for this `state_type` |

```cpp
int do_encoding() const noexcept;
```

**9** *Returns:* `-1` if the encoding of the `externT` sequence is state-dependent; else the constant number of `externT` characters needed to produce an internal character; or `0` if this number is not a constant.[^3]

```cpp
bool do_always_noconv() const noexcept;
```

**10** *Returns:* `true` if `do_in()` and `do_out()` return `noconv` for all valid argument values. `codecvt<char, char, mbstate_t>` returns `true`.

```cpp
int do_length(stateT& state, const externT* from, const externT* from_end, size_t max) const;
```

**11** *Preconditions:* `(from <= from_end)` is well-defined and `true`; `state` is initialized, if at the beginning of a sequence, or else is equal to the result of converting the preceding characters in the sequence.

**12** *Effects:* The effect on the `state` argument is as if it called `do_in(state, from, from_end, from, to, to+max, to)` for `to` pointing to a buffer of at least `max` elements.

**13** *Returns:* `(from_next-from)` where `from_next` is the largest value in the range `[from, from_end]` such that the sequence of values in the range `[from, from_next)` represents `max` or fewer valid complete characters of type `internT`. The specialization `codecvt<char, char, mbstate_t>`, returns the lesser of `max` and `(from_end-from)`.

```cpp
int do_max_length() const noexcept;
```

**14** *Returns:* The maximum value that `do_length(state, from, from_end, 1)` can return for any valid range `[from, from_end)` and `stateT` value `state`. The specialization `codecvt<char, char, mbstate_t>::do_max_length()` returns 1.

[^1]: Informally, this means that `basic_filebuf` assumes that the mappings from internal to external characters is 1 to N: a `codecvt` facet that is used by `basic_filebuf` must be able to translate characters one internal character at a time.
[^2]: Typically these will be characters to return the state to `stateT()`.
[^3]: If `encoding()` yields `-1`, then more than `max_length()` `externT` elements may be consumed when producing a single `internT` character, and additional `externT` elements may appear at the end of a sequence after those that yield the final `internT` character.
