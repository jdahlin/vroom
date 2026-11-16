# 30.10.1 Constructors [re.results.const]

[Contents](../../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10 Class template `match_results` \[re.results\]](re.results.md) · next: [30.10.2 State \[re.results.state\]](re.results.state.md)

```cpp
explicit match_results(const Allocator& a);
```

**1** *Postconditions:* `ready()` returns `false`. `size()` returns `0`.

```cpp
match_results(match_results&& m) noexcept;
```

**2** *Effects:* The stored `Allocator` value is move constructed from `m.get_allocator()`.

**3** *Postconditions:* As specified in [Table 140](re.results.const.md#tab:re.results.const).

```cpp
match_results& operator=(const match_results& m);
```

**4** *Postconditions:* As specified in [Table 140](re.results.const.md#tab:re.results.const).

```cpp
match_results& operator=(match_results&& m);
```

**5** *Postconditions:* As specified in [Table 140](re.results.const.md#tab:re.results.const).

<a id="tab:re.results.const"></a>
**Table 140: `match_results` assignment operator effects** [tab:re.results.const]

| Element | Value |
| --- | --- |
| `ready()` | `m.ready()` |
| `size()` | `m.size()` |
| `str(n)` | `m.str(n)` for all integers `n < m.size()` |
| `prefix()` | `m.prefix()` |
| `suffix()` | `m.suffix()` |
| `(*this)[n]` | `m[n]` for all integers `n < m.size()` |
| `length(n)` | `m.length(n)` for all integers `n < m.size()` |
| `position(n)` | `m.position(n)` for all integers `n < m.size()` |
