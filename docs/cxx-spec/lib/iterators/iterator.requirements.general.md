# 23.3.1 In general [iterator.requirements.general]

[Contents](../../README.md) · up: [23.3 Iterator requirements \[iterator.requirements\]](iterator.requirements.md) · prev: [23.3 Iterator requirements \[iterator.requirements\]](iterator.requirements.md) · next: [23.3.2 Associated types \[iterator.assoc.types\]](iterator.assoc.types.md)

**1** Iterators are a generalization of pointers that allow a C++ program to work with different data structures (for example, containers and ranges) in a uniform manner. To be able to construct template algorithms that work correctly and efficiently on different types of data structures, the library formalizes not just the interfaces but also the semantics and complexity assumptions of iterators. An input iterator `i` supports the expression `*i`, resulting in a value of some object type `T`, called the *value type* of the iterator. An output iterator `i` has a non-empty set of types that are `indirectly_writable` to the iterator; for each such type `T`, the expression `*i = o` is valid where `o` is a value of type `T`. For every iterator type `X`, there is a corresponding signed integer-like type ([[iterator.concept.winc]](iterator.concept.winc.md)) called the *difference type* of the iterator.

**2** Since iterators are an abstraction of pointers, their semantics are a generalization of most of the semantics of pointers in C++. This ensures that every function template that takes iterators works as well with regular pointers. This document defines six categories of iterators, according to the operations defined on them: *input iterators*, *output iterators*, *forward iterators*, *bidirectional iterators*, *random access iterators*, and *contiguous iterators*, as shown in [Table 83](iterator.requirements.general.md#tab:iterators.relations).

<a id="tab:iterators.relations"></a>
**Table 83: Relations among iterator categories** [tab:iterators.relations]

|  |  |  |  |  |
| --- | --- | --- | --- | --- |
| **Contiguous** | → **Random Access** | → **Bidirectional** | → **Forward** | → **Input** |
|  |  |  |  | → **Output** |

**3** The six categories of iterators correspond to the iterator concepts

- `input_iterator` ([[iterator.concept.input]](iterator.concept.input.md)),
- `output_iterator` ([[iterator.concept.output]](iterator.concept.output.md)),
- `forward_iterator` ([[iterator.concept.forward]](iterator.concept.forward.md)),
- `bidirectional_iterator` ([[iterator.concept.bidir]](iterator.concept.bidir.md))
- `random_access_iterator` ([[iterator.concept.random.access]](iterator.concept.random.access.md)), and
- `contiguous_iterator` ([[iterator.concept.contiguous]](iterator.concept.contiguous.md)),

respectively. The generic term *iterator* refers to any type that models the `input_or_output_iterator` concept ([[iterator.concept.iterator]](iterator.concept.iterator.md)).

**4** Forward iterators meet all the requirements of input iterators and can be used whenever an input iterator is specified; Bidirectional iterators also meet all the requirements of forward iterators and can be used whenever a forward iterator is specified; Random access iterators also meet all the requirements of bidirectional iterators and can be used whenever a bidirectional iterator is specified; Contiguous iterators also meet all the requirements of random access iterators and can be used whenever a random access iterator is specified.

**5** Iterators that further meet the requirements of output iterators are called *mutable iterators*. Nonmutable iterators are referred to as *constant iterators*.

**6** In addition to the requirements in this subclause, the nested *typedef-name*s specified in [[iterator.traits]](iterator.traits.md) shall be provided for the iterator type. [*Note 1*: Either the iterator type must provide the *typedef-name*s directly (in which case `iterator_traits` pick them up automatically), or an `iterator_traits` specialization must provide them. — *end note*]

**7** Just as a regular pointer to an array guarantees that there is a pointer value pointing past the last element of the array, so for any iterator type there is an iterator value that points past the last element of a corresponding sequence. Such a value is called a *past-the-end value*. Values of an iterator `i` for which the expression `*i` is defined are called *dereferenceable*. The library never assumes that past-the-end values are dereferenceable. Iterators can also have singular values that are not associated with any sequence. [*Example 1*: After the declaration of an uninitialized pointer `x` (as with `int* x;`), `x` must always be assumed to have a singular value of a pointer. — *end example*] Results of most expressions are undefined for singular values; the only exceptions are destroying an iterator that holds a singular value, the assignment of a non-singular value to an iterator that holds a singular value, and, for iterators that meet the *Cpp17DefaultConstructible* requirements, using a value-initialized iterator as the source of a copy or move operation. [*Note 2*: This guarantee is not offered for default-initialization, although the distinction only matters for types with trivial default constructors such as pointers or aggregates holding pointers. — *end note*] In these cases the singular value is overwritten the same way as any other value. Dereferenceable values are always non-singular.

**8** Most of the library’s algorithmic templates that operate on data structures have interfaces that use ranges. A *range* is an iterator and a *sentinel* that designate the beginning and end of the computation, or an iterator and a count that designate the beginning and the number of elements to which the computation is to be applied.[^1]

**9** An iterator and a sentinel denoting a range are comparable. A range `[i, s)` is empty if `i == s`; otherwise, `[i, s)` refers to the elements in the data structure starting with the element pointed to by `i` and up to but not including the element, if any, pointed to by the first iterator `j` such that `j == s`.

**10** A sentinel `s` is called *reachable from* an iterator `i` if and only if there is a finite sequence of applications of the expression `++i` that makes `i == s`. If `s` is reachable from `i`, `[i, s)` denotes a valid range.

**11** A *counted range* `i` + `[0, n)` is empty if `n == 0`; otherwise, `i` + `[0, n)` refers to the `n` elements in the data structure starting with the element pointed to by `i` and up to but not including the element, if any, pointed to by the result of `n` applications of `++i`. A counted range `i` + `[0, n)` is valid if and only if `n == 0`; or `n` is positive, `i` is dereferenceable, and `++i` + `[0, --n)` is valid.

**12** The result of the application of library functions to invalid ranges is undefined.

**13** All the categories of iterators require only those functions that are realizable for a given category in constant time (amortized). Therefore, requirement tables and concept definitions for the iterators do not specify complexity.

**14** Destruction of a non-forward iterator may invalidate pointers and references previously obtained from that iterator.

**15** An *invalid iterator* is an iterator that may be singular.[^2]

**16** Iterators are called *constexpr iterators* if all operations provided to meet iterator category requirements are constexpr functions. [*Note 3*: For example, the types “pointer to `int`” and `reverse_iterator<int*>` are constexpr iterators. — *end note*]

[^1]: The sentinel denoting the end of a range may have the same type as the iterator denoting the beginning of the range, or a different type.
[^2]: This definition applies to pointers, since pointers are iterators. The effect of dereferencing an iterator that has been invalidated is undefined.
