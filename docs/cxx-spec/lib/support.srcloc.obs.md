# 17.8.2.2 Observers [support.srcloc.obs]

[Contents](../README.md) · up: [17.8.2 Class `source_location` \[support.srcloc.class\]](support.srcloc.class.md) · prev: [17.8.2.1 Creation \[support.srcloc.cons\]](support.srcloc.cons.md) · next: [17.9 Exception handling \[support.exception\]](support.exception.md)

```cpp
constexpr uint_least32_t line() const noexcept;
```

**1** *Returns:* `line_`.

```cpp
constexpr uint_least32_t column() const noexcept;
```

**2** *Returns:* `column_`.

```cpp
constexpr const char* file_name() const noexcept;
```

**3** *Returns:* `file_name_`.

```cpp
constexpr const char* function_name() const noexcept;
```

**4** *Returns:* `function_name_`.
