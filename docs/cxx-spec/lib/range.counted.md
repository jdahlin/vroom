# 24.7.12 Counted view [range.counted]

[Contents](../README.md) · up: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md) · prev: [24.7.11.5 Class template `split_view::inner-iterator` \[range.split.inner\]](range.split.inner.md) · next: [24.7.13 Common view \[range.common\]](range.common.md)

**1** A counted view presents a `view` of the elements of the counted range ([[iterator.requirements.general]](iterator.requirements.general.md)) `i` + `[0, n)` for an iterator `i` and non-negative integer `n`.

**2** The name `views::counted` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Let `E` and `F` be expressions, let `T` be `decay_t<decltype((E))>`, and let `D` be `iter_difference_t<T>`. If `decltype((F))` does not model `convertible_to<D>`, `views::counted(E, F)` is ill-formed. [*Note 1*: This case can result in substitution failure when `views::counted(E, F)` appears in the immediate context of a template instantiation. — *end note*] Otherwise, `views::counted(E, F)` is expression-equivalent to:

- If `T` models `contiguous_iterator`, then `span{to_address(E), static_cast<D>(F)}`.
- Otherwise, if `T` models `random_access_iterator`, then `subrange{E, E + static_cast<D>(F)}`, except that `E` is evaluated only once.
- Otherwise, `subrange{counted_iterator{E, F}, default_sentinel}`.
