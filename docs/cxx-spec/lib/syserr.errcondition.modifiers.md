# 19.5.4.3 Modifiers [syserr.errcondition.modifiers]

[Contents](../README.md) · up: [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md) · prev: [19.5.4.2 Constructors \[syserr.errcondition.constructors\]](syserr.errcondition.constructors.md) · next: [19.5.4.4 Observers \[syserr.errcondition.observers\]](syserr.errcondition.observers.md)

```cpp
void assign(int val, const error_category& cat) noexcept;
```

**1** *Postconditions:* `val_ == val` and `cat_ == &cat`.

```cpp
template<class ErrorConditionEnum>
  error_condition& operator=(ErrorConditionEnum e) noexcept;
```

**2** *Constraints:* `is_error_condition_enum_v<ErrorConditionEnum>` is `true`.

**3** *Postconditions:* `*this == make_error_condition(e)`.

**4** *Returns:* `*this`.

```cpp
void clear() noexcept;
```

**5** *Postconditions:* `value() == 0` and `category() == generic_category()`.
