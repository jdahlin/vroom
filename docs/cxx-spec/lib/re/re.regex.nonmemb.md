# 30.8.6 Non-member functions [re.regex.nonmemb]

[Contents](../../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8.5 Swap \[re.regex.swap\]](re.regex.swap.md) · next: [30.9 Class template `sub_match` \[re.submatch\]](re.submatch.md)

```cpp
template<class charT, class traits>
  void swap(basic_regex<charT, traits>& lhs, basic_regex<charT, traits>& rhs);
```

**1** *Effects:* Calls `lhs.swap(rhs)`.
