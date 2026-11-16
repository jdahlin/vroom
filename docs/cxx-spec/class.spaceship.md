# 11.11.3 Three-way comparison [class.spaceship]

[Contents](README.md) · up: [11.11 Comparisons \[class.compare\]](class.compare.md) · prev: [11.11.2 Equality operator \[class.eq\]](class.eq.md) · next: [11.11.4 Secondary comparison operators \[class.compare.secondary\]](class.compare.secondary.md)

**1** The *synthesized three-way comparison* of type `R` ([[cmp.categories]](lib/cmp.categories.md)) of glvalues `a` and `b` of the same type is defined as follows:

- If `a <=> b` is usable ([[class.compare.default]](class.compare.default.md)), `static_cast<R>(a <=> b)`.

- Otherwise, if overload resolution for `a <=> b` is performed and finds at least one viable candidate, the synthesized three-way comparison is not defined.

- Otherwise, if `R` is not a comparison category type, or either the expression `a == b` or the expression `a < b` is not usable, the synthesized three-way comparison is not defined.

- Otherwise, if `R` is `strong_ordering`, then

  ```cpp
  a == b ? strong_ordering::equal :
  a < b  ? strong_ordering::less :
           strong_ordering::greater
  ```

- Otherwise, if `R` is `weak_ordering`, then

  ```cpp
  a == b ? weak_ordering::equivalent :
  a < b  ? weak_ordering::less :
           weak_ordering::greater
  ```

- Otherwise (when `R` is `partial_ordering`),

  ```cpp
  a == b ? partial_ordering::equivalent :
  a < b  ? partial_ordering::less :
  b < a  ? partial_ordering::greater :
           partial_ordering::unordered
  ```

[*Note 1*: A synthesized three-way comparison may be ill-formed if overload resolution finds usable candidates that do not otherwise meet the requirements implied by the defined expression. — *end note*]

**2** Let `R` be the declared return type of a defaulted three-way comparison operator function, and let `x`<sub>*i*</sub> be the elements of the expanded list of subobjects for an object `x` of type `C`.

- If `R` is `auto`, then let *cv*<sub>*i*</sub> `R`<sub>*i*</sub> be the type of the expression `x`<sub>*i*</sub>`<=>``x`<sub>*i*</sub>. The operator function is defined as deleted if that expression is not usable or if `R`<sub>*i*</sub> is not a comparison category type ([[cmp.categories.pre]](lib/cmp.categories.pre.md)) for any *i*. The return type is deduced as the common comparison type (see below) of `R`<sub>0</sub>, `R`<sub>1</sub>, …, `R`<sub>*n*-1</sub>.
- Otherwise, `R` shall not contain a placeholder type. If the synthesized three-way comparison of type `R` between any objects `x`<sub>*i*</sub> and `x`<sub>*i*</sub> is not defined, the operator function is defined as deleted.

**3** The return value `V` of type `R` of the defaulted three-way comparison operator function with parameters `x` and `y` of the same type is determined by comparing corresponding elements `x`<sub>*i*</sub> and `y`<sub>*i*</sub> in the expanded lists of subobjects for `x` and `y` (in increasing index order) until the first index *i* where the synthesized three-way comparison of type `R` between `x`<sub>*i*</sub> and `y`<sub>*i*</sub> yields a result value `v`<sub>*i*</sub> where `v`<sub>*i*</sub> `!=` 0, contextually converted to `bool`, yields `true`; `V` is a copy of `v`<sub>*i*</sub>. If no such index exists, `V` is `static_cast<R>(std::strong_ordering::equal)`.

**4** The *common comparison type* `U` of a possibly-empty list of *n* comparison category types `T`<sub>0</sub>, `T`<sub>1</sub>, …, `T`<sub>*n*-1</sub> is defined as follows:

- If at least one `T`<sub>*i*</sub> is `std::partial_ordering`, `U` is `std::partial_ordering` ([[cmp.partialord]](lib/cmp.partialord.md)).
- Otherwise, if at least one `T`<sub>*i*</sub> is `std::weak_ordering`, `U` is `std::weak_ordering` ([[cmp.weakord]](lib/cmp.weakord.md)).
- Otherwise, `U` is `std::strong_ordering` ([[cmp.strongord]](lib/cmp.strongord.md)). [*Note 2*: In particular, this is the result when *n* is 0. — *end note*]
