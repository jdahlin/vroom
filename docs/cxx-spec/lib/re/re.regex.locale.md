# 30.8.4 Locale [re.regex.locale]

[Contents](../../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8.3 Constant operations \[re.regex.operations\]](re.regex.operations.md) · next: [30.8.5 Swap \[re.regex.swap\]](re.regex.swap.md)

```cpp
locale_type imbue(locale_type loc);
```

**1** *Effects:* Returns the result of `traits_inst.imbue(loc)` where `traits_inst` is a (default-initialized) instance of the template type argument `traits` stored within the object. After a call to `imbue` the `basic_regex` object does not match any character sequence.

```cpp
locale_type getloc() const;
```

**2** *Effects:* Returns the result of `traits_inst.getloc()` where `traits_inst` is a (default-initialized) instance of the template parameter `traits` stored within the object.
