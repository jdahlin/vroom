# 17.8.2 Class `source_location` [support.srcloc.class]

[Contents](../README.md) · up: [17.8 Source location \[support.srcloc\]](support.srcloc.md) · prev: [17.8.1 Header `<source_location>` synopsis \[source.location.syn\]](source.location.syn.md) · next: [17.8.2.1 Creation \[support.srcloc.cons\]](support.srcloc.cons.md)

```cpp
namespace std {
  struct source_location {
    // source location construction
    static consteval source_location current() noexcept;
    constexpr source_location() noexcept;

    // source location field access
    constexpr uint_least32_t line() const noexcept;
    constexpr uint_least32_t column() const noexcept;
    constexpr const char* file_name() const noexcept;
    constexpr const char* function_name() const noexcept;

  private:
    uint_least32_t line_;               // exposition only
    uint_least32_t column_;             // exposition only
    const char* file_name_;             // exposition only
    const char* function_name_;         // exposition only
  };
}
```

**1** The type `source_location` meets the *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, and *Cpp17Destructible* requirements ([[utility.arg.requirements]](utility.arg.requirements.md)). Lvalues of type `source_location` are swappable ([[swappable.requirements]](swappable.requirements.md)). All of the following conditions are `true`:

- `is_nothrow_move_constructible_v<source_location>`
- `is_nothrow_move_assignable_v<source_location>`
- `is_nothrow_swappable_v<source_location>`

[*Note 1*: The intent of `source_location` is to have a small size and efficient copying. It is unspecified whether the copy/move constructors and the copy/move assignment operators are trivial and/or constexpr. — *end note*]

**2** The data members `file_name_` and `function_name_` always each refer to an NTBS.

**3** The copy/move constructors and the copy/move assignment operators of `source_location` meet the following postconditions: Given two objects `lhs` and `rhs` of type `source_location`, where `lhs` is a copy/move result of `rhs`, and where `rhs_p` is a value denoting the state of `rhs` before the corresponding copy/move operation, then each of the following conditions is `true`:

- `strcmp(lhs.file_name(), rhs_p.file_name()) == 0`
- `strcmp(lhs.function_name(), rhs_p.function_name()) == 0`
- `lhs.line() == rhs_p.line()`
- `lhs.column() == rhs_p.column()`

## Subclauses

- [17.8.2.1 Creation \[support.srcloc.cons\]](support.srcloc.cons.md)
- [17.8.2.2 Observers \[support.srcloc.obs\]](support.srcloc.obs.md)
