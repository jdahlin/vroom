# 30.10.3 Size [re.results.size]

[Contents](../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.2 State \[re.results.state\]](re.results.state.md) · next: [30.10.4 Element access \[re.results.acc\]](re.results.acc.md)

```cpp
size_type size() const;
```

**1** *Returns:* One plus the number of marked sub-expressions in the regular expression that was matched if `*this` represents the result of a successful match. Otherwise returns `0`. [*Note 1*: The state of a `match_results` object can be modified only by passing that object to `regex_match` or `regex_search`. Subclauses [[re.alg.match]](re.alg.match.md) and [[re.alg.search]](re.alg.search.md) specify the effects of those algorithms on their `match_results` arguments. — *end note*]

```cpp
size_type max_size() const;
```

**2** *Returns:* The maximum number of `sub_match` elements that can be stored in `*this`.

```cpp
[[nodiscard]] bool empty() const;
```

**3** *Returns:* `size() == 0`.
