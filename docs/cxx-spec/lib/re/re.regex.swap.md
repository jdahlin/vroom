# 30.8.5 Swap [re.regex.swap]

[Contents](../../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8.4 Locale \[re.regex.locale\]](re.regex.locale.md) · next: [30.8.6 Non-member functions \[re.regex.nonmemb\]](re.regex.nonmemb.md)

```cpp
void swap(basic_regex& e);
```

**1** *Effects:* Swaps the contents of the two regular expressions.

**2** *Postconditions:* `*this` contains the regular expression that was in `e`, `e` contains the regular expression that was in `*this`.

**3** *Complexity:* Constant time.
