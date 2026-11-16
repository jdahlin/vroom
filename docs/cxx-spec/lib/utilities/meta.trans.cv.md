# 20.15.7.1 Const-volatile modifications [meta.trans.cv]

[Contents](../../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · next: [20.15.7.2 Reference modifications \[meta.trans.ref\]](meta.trans.ref.md)

<a id="tab:meta.trans.cv"></a>
**Table 52: Const-volatile modifications** [tab:meta.trans.cv]

| Template | Comments |
| --- | --- |
| `template<class T> struct remove_const;` | The member typedef `type` names the same type as `T` except that any top-level const-qualifier has been removed. [*Example 1*: `remove_const_t<const volatile int>` evaluates to `volatile int`, whereas `remove_const_t<const int*>` evaluates to `const int*`. — *end example*] |
| `template<class T> struct remove_volatile;` | The member typedef `type` names the same type as `T` except that any top-level volatile-qualifier has been removed. [*Example 2*: `remove_volatile_t<const volatile int>` evaluates to `const int`, whereas `remove_volatile_t<volatile int*>` evaluates to `volatile int*`. — *end example*] |
| `template<class T> struct remove_cv;` | The member typedef `type` shall be the same as `T` except that any top-level cv-qualifier has been removed. [*Example 3*: `remove_cv_t<const volatile int>` evaluates to `int`, whereas `remove_cv_t<const volatile int*>` evaluates to `const volatile int*`. — *end example*] |
| `template<class T> struct add_const;` | If `T` is a reference, function, or top-level const-qualified type, then `type` names the same type as `T`, otherwise `T const`. |
| `template<class T> struct add_volatile;` | If `T` is a reference, function, or top-level volatile-qualified type, then `type` names the same type as `T`, otherwise `T volatile`. |
| `template<class T> struct add_cv;` | The member typedef `type` names the same type as `add_const_t<add_volatile_t<T>>`. |
