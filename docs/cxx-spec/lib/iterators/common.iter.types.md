# 23.5.4.2 Associated types [common.iter.types]

[Contents](../../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.1 Class template `common_iterator` \[common.iterator\]](common.iterator.md) · next: [23.5.4.3 Constructors and conversions \[common.iter.const\]](common.iter.const.md)

**1** The nested *typedef-name*s of the specialization of `iterator_traits` for `common_iterator<I, S>` are defined as follows.

- `iterator_concept` denotes `forward_iterator_tag` if `I` models `forward_iterator`; otherwise it denotes `input_iterator_tag`.
- `iterator_category` denotes `forward_iterator_tag` if `iterator_traits<I>::iterator_category` models `derived_from<forward_iterator_tag>`; otherwise it denotes `input_iterator_tag`.
- If the expression `a.operator->()` is well-formed, where `a` is an lvalue of type `const common_iterator<I, S>`, then `pointer` denotes the type of that expression. Otherwise, `pointer` denotes `void`.
