# 20.2.6 Function template `declval` [declval]

[Contents](../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.5 Function template `as_const` \[utility.as.const\]](utility.as.const.md) · next: [20.2.7 Integer comparison functions \[utility.intcmp\]](utility.intcmp.md)

**1** The library provides the function template `declval` to simplify the definition of expressions which occur as unevaluated operands ([[expr.prop]](../expr.prop.md)).

```cpp
template<class T> add_rvalue_reference_t<T> declval() noexcept;    // as unevaluated operand
```

**2** *Mandates:* This function is not odr-used ([[basic.def.odr]](../basic.def.odr.md)).

**3** *Remarks:* The template parameter `T` of `declval` may be an incomplete type.

**4** [*Example 1*:

```cpp
template<class To, class From> decltype(static_cast<To>(declval<From>())) convert(From&&);
```

declares a function template `convert` which only participates in overloading if the type `From` can be explicitly converted to type `To`. For another example see class template `common_type` ([[meta.trans.other]](meta.trans.other.md)). — *end example*]
