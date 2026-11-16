# 20.7.7 Visitation [variant.visit]

[Contents](../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.6 Relational operators \[variant.relops\]](variant.relops.md) · next: [20.7.8 Class `monostate` \[variant.monostate\]](variant.monostate.md)

```cpp
template<class Visitor, class... Variants>
  constexpr see below visit(Visitor&& vis, Variants&&... vars);
template<class R, class Visitor, class... Variants>
  constexpr R visit(Visitor&& vis, Variants&&... vars);
```

**1** Let *n* be `sizeof...(Variants)`. Let `m` be a pack of *n* values of type `size_t`. Such a pack is called valid if 0 ≤ `m`<sub>*i*</sub> &lt; `variant_size_v<remove_reference_t<Variants`<sub>*i*</sub>`>>` for all 0 ≤ *i* &lt; *n*. For each valid pack `m`, let *e*(`m`) denote the expression:

```cpp
INVOKE(std::forward<Visitor>(vis), get<m>(std::forward<Variants>(vars))...) // see [func.require]
```

for the first form and

```cpp
INVOKE<R>(std::forward<Visitor>(vis), get<m>(std::forward<Variants>(vars))...) // see [func.require]
```

for the second form.

**2** *Mandates:* For each valid pack `m`, *e*(`m`) is a valid expression. All such expressions are of the same type and value category.

**3** *Returns:* *e*(`m`), where `m` is the pack for which `m`<sub>*i*</sub> is `vars_i.index()` for all 0 ≤ *i* &lt; *n*. The return type is `decltype(`*e*(`m`)`)` for the first form.

**4** *Throws:* `bad_variant_access` if any `variant` in `vars` is `valueless_by_exception()`.

**5** *Complexity:* For *n* ≤ 1, the invocation of the callable object is implemented in constant time, i.e., for *n* = 1, it does not depend on the number of alternative types of `Variants`<sub>0</sub>. For *n* > 1, the invocation of the callable object has no complexity requirements.
