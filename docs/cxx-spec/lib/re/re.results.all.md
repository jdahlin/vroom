# 30.10.6 Allocator [re.results.all]

[Contents](../../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.5 Formatting \[re.results.form\]](re.results.form.md) · next: [30.10.7 Swap \[re.results.swap\]](re.results.swap.md)

```cpp
allocator_type get_allocator() const;
```

**1** *Returns:* A copy of the Allocator that was passed to the object’s constructor or, if that allocator has been replaced, a copy of the most recent replacement.
