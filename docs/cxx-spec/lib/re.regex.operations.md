# 30.8.3 Constant operations [re.regex.operations]

[Contents](../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8.2 Assignment \[re.regex.assign\]](re.regex.assign.md) · next: [30.8.4 Locale \[re.regex.locale\]](re.regex.locale.md)

```cpp
unsigned mark_count() const;
```

**1** *Effects:* Returns the number of marked sub-expressions within the regular expression.

```cpp
flag_type flags() const;
```

**2** *Effects:* Returns a copy of the regular expression syntax flags that were passed to the object’s constructor or to the last call to `assign`.
