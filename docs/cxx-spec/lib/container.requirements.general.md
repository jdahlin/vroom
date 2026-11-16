# 22.2.1 General container requirements [container.requirements.general]

[Contents](../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · next: [22.2.2 Container data races \[container.requirements.dataraces\]](container.requirements.dataraces.md)

**1** Containers are objects that store other objects. They control allocation and deallocation of these objects through constructors, destructors, insert and erase operations.

**2** All of the complexity requirements in this Clause are stated solely in terms of the number of operations on the contained objects. [*Example 1*: The copy constructor of type `vector<vector<int>>` has linear complexity, even though the complexity of copying each contained `vector<int>` is itself linear. — *end example*]

**3** For the components affected by this subclause that declare an `allocator_type`, objects stored in these components shall be constructed using the function `allocator_traits<allocator_type>::rebind_traits<U>::construct` and destroyed using the function `allocator_traits<allocator_type>::rebind_traits<U>::destroy` ([[allocator.traits.members]](allocator.traits.members.md)), where `U` is either `allocator_type::value_type` or an internal type used by the container. These functions are called only for the container’s element type, not for internal types used by the container. [*Note 1*: This means, for example, that a node-based container might need to construct nodes containing aligned buffers and call `construct` to place the element into the buffer. — *end note*]

**4** In Tables [[tab:container.req]](container.requirements.general.md#tab:container.req), [[tab:container.rev.req]](container.requirements.general.md#tab:container.rev.req), and [[tab:container.opt]](container.requirements.general.md#tab:container.opt) `X` denotes a container class containing objects of type `T`, `a` and `b` denote values of type `X`, `i` and `j` denote values of type (possibly const) `X::iterator`, `u` denotes an identifier, `r` denotes a non-const value of type `X`, and `rv` denotes a non-const rvalue of type `X`.

<a id="tab:container.req"></a>
**Table 73: Container requirements** [tab:container.req]

| Expression | Return type | Operational | Assertion/note | Complexity |
| --- | --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |  |
| `X::value_type` | `T` |  | *Preconditions:* `T` is *Cpp17Erasable* from `X` (see [[container.requirements.general]](container.requirements.general.md), below) | compile time |
| `X::reference` | `T&` |  |  | compile time |
| `X::const_reference` | `const T&` |  |  | compile time |
| `X::iterator` | iterator type whose value type is `T` |  | any iterator category that meets the forward iterator requirements. convertible to `X::const_iterator`. | compile time |
| `X::const_iterator` | constant iterator type whose value type is `T` |  | any iterator category that meets the forward iterator requirements. | compile time |
| `X::difference_type` | signed integer type |  | is identical to the difference type of `X::iterator` and `X::const_iterator` | compile time |
| `X::size_type` | unsigned integer type |  | `size_type` can represent any non-negative value of `difference_type` | compile time |
| `X u;` |  |  | *Postconditions:* `u.empty()` | constant |
| `X()` |  |  | *Postconditions:* `X().empty()` | constant |
| `X(a)` |  |  | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` (see below).<br>*Postconditions:* `a == X(a)`. | linear |
| `X u(a);`<br>`X u = a;` |  |  | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` (see below).<br>*Postconditions:* `u == a` | linear |
| `X u(rv);`<br>`X u = rv;` |  |  | *Postconditions:* `u` is equal to the value that `rv` had before this construction | (Note B) |
| `a = rv` | `X&` | All existing elements of `a` are either move assigned to or destroyed | *Postconditions:* `a` is equal to the value that `rv` had before this assignment | linear |
| `a.~X()` | `void` |  | *Effects:* destroys every element of `a`; any memory obtained is deallocated. | linear |
| `a.begin()` | `iterator`; `const_iterator` for constant `a` |  |  | constant |
| `a.end()` | `iterator`; `const_iterator` for constant `a` |  |  | constant |
| `a.cbegin()` | `const_iterator` | `const_cast<X const&>(a).begin();` |  | constant |
| `a.cend()` | `const_iterator` | `const_cast<X const&>(a).end();` |  | constant |
| `i <=> j` | `strong_ordering` |  | *Constraints:* `X::iterator` meets the random access iterator requirements. | constant |
| `a == b` | convertible to `bool` | `==` is an equivalence relation. `equal(a.begin(), a.end(), b.begin(), b.end())` | *Preconditions:* `T` meets the *Cpp17EqualityComparable* requirements | Constant if `a.size() != b.size()`, linear otherwise |
| `a != b` | convertible to `bool` | Equivalent to `!(a == b)` |  | linear |
| `a.swap(b)` | `void` |  | *Effects:* exchanges the contents of `a` and `b` | (Note A) |
| `swap(a, b)` | `void` | Equivalent to `a.swap(b)` |  | (Note A) |
| `r = a` | `X&` |  | *Postconditions:* `r == a`. | linear |
| `a.size()` | `size_type` | `distance(a.begin(), a.end())` |  | constant |
| `a.max_size()` | `size_type` | `distance(begin(), end())` for the largest possible container |  | constant |
| `a.empty()` | convertible to `bool` | `a.begin() == a.end()` |  | constant |

Those entries marked “(Note A)” or “(Note B)” have linear complexity for `array` and have constant complexity for all other standard containers. [*Note 2*: The algorithm `equal()` is defined in [[algorithms]](algorithms.md). — *end note*]

**5** The member function `size()` returns the number of elements in the container. The number of elements is defined by the rules of constructors, inserts, and erases.

**6** `begin()` returns an iterator referring to the first element in the container. `end()` returns an iterator which is the past-the-end value for the container. If the container is empty, then `begin() == end()`.

**7** In the expressions

```cpp
i == j
i != j
i < j
i <= j
i >= j
i > j
i <=> j
i - j
```

where `i` and `j` denote objects of a container’s `iterator` type, either or both may be replaced by an object of the container’s `const_iterator` type referring to the same element with no change in semantics.

**8** Unless otherwise specified, all containers defined in this clause obtain memory using an allocator (see [[allocator.requirements]](allocator.requirements.md)). [*Note 3*: In particular, containers and iterators do not store references to allocated elements other than through the allocator’s pointer type, i.e., as objects of type `P` or `pointer_traits<P>::template rebind<unspecified>`, where `P` is `allocator_traits<allocator_type>::pointer`. — *end note*] Copy constructors for these container types obtain an allocator by calling `allocator_traits<allocator_type>::select_on_container_copy_construction` on the allocator belonging to the container being copied. Move constructors obtain an allocator by move construction from the allocator belonging to the container being moved. Such move construction of the allocator shall not exit via an exception. All other constructors for these container types take a `const allocator_type&` argument. [*Note 4*: If an invocation of a constructor uses the default value of an optional allocator argument, then the allocator type must support value-initialization. — *end note*] A copy of this allocator is used for any memory allocation and element construction performed, by these constructors and by all member functions, during the lifetime of each container object or until the allocator is replaced. The allocator may be replaced only via assignment or `swap()`. Allocator replacement is performed by copy assignment, move assignment, or swapping of the allocator only if `allocator_traits<allocator_type>::propagate_on_container_copy_assignment::value`, `allocator_traits<allocator_type>::propagate_on_container_move_assignment::value`, or `allocator_traits<allocator_type>::propagate_on_container_swap::value` is `true` within the implementation of the corresponding container operation. In all container types defined in this Clause, the member `get_allocator()` returns a copy of the allocator used to construct the container or, if that allocator has been replaced, a copy of the most recent replacement.

**9** The expression `a.swap(b)`, for containers `a` and `b` of a standard container type other than `array`, shall exchange the values of `a` and `b` without invoking any move, copy, or swap operations on the individual container elements. Lvalues of any `Compare`, `Pred`, or `Hash` types belonging to `a` and `b` shall be swappable and shall be exchanged by calling `swap` as described in [[swappable.requirements]](swappable.requirements.md). If `allocator_traits<allocator_type>::propagate_on_container_swap::value` is `true`, then lvalues of type `allocator_type` shall be swappable and the allocators of `a` and `b` shall also be exchanged by calling `swap` as described in [[swappable.requirements]](swappable.requirements.md). Otherwise, the allocators shall not be swapped, and the behavior is undefined unless `a.get_allocator() == b.get_allocator()`. Every iterator referring to an element in one container before the swap shall refer to the same element in the other container after the swap. It is unspecified whether an iterator with value `a.end()` before the swap will have value `b.end()` after the swap.

**10** If the iterator type of a container belongs to the bidirectional or random access iterator categories ([[iterator.requirements]](iterator.requirements.md)), the container is called *reversible* and meets the additional requirements in [Table 74](container.requirements.general.md#tab:container.rev.req).

<a id="tab:container.rev.req"></a>
**Table 74: Reversible container requirements** [tab:container.rev.req]

| Expression | Return type | Assertion/note | Complexity |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `X::reverse_iterator` | iterator type whose value type is `T` | `reverse_iterator<iterator>` | compile time |
| `X::const_reverse_iterator` | constant iterator type whose value type is `T` | `reverse_iterator<const_iterator>` | compile time |
| `a.rbegin()` | `reverse_iterator; const_reverse_iterator` for constant `a` | `reverse_iterator(end())` | constant |
| `a.rend()` | `reverse_iterator; const_reverse_iterator` for constant `a` | `reverse_iterator(begin())` | constant |
| `a.crbegin()` | `const_reverse_iterator` | `const_cast<X const&>(a).rbegin()` | constant |
| `a.crend()` | `const_reverse_iterator` | `const_cast<X const&>(a).rend()` | constant |

**11** Unless otherwise specified (see [[associative.reqmts.except]](associative.reqmts.except.md), [[unord.req.except]](unord.req.except.md), [[deque.modifiers]](deque.modifiers.md), and [[vector.modifiers]](vector.modifiers.md)) all container types defined in this Clause meet the following additional requirements:

- if an exception is thrown by an `insert()` or `emplace()` function while inserting a single element, that function has no effects.
- if an exception is thrown by a `push_back()`, `push_front()`, `emplace_back()`, or `emplace_front()` function, that function has no effects.
- no `erase()`, `clear()`, `pop_back()` or `pop_front()` function throws an exception.
- no copy constructor or assignment operator of a returned iterator throws an exception.
- no `swap()` function throws an exception.
- no `swap()` function invalidates any references, pointers, or iterators referring to the elements of the containers being swapped. [*Note 5*: The `end()` iterator does not refer to any element, so it may be invalidated. — *end note*]

**12** Unless otherwise specified (either explicitly or by defining a function in terms of other functions), invoking a container member function or passing a container as an argument to a library function shall not invalidate iterators to, or change the values of, objects within that container.

**13** A *contiguous container* is a container whose member types `iterator` and `const_iterator` meet the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](random.access.iterators.md)) and model `contiguous_iterator` ([[iterator.concept.contiguous]](iterator.concept.contiguous.md)).

**14** [Table 75](container.requirements.general.md#tab:container.opt) lists operations that are provided for some types of containers but not others. Those containers for which the listed operations are provided shall implement the semantics described in [Table 75](container.requirements.general.md#tab:container.opt) unless otherwise stated. If the iterators passed to `lexicographical_compare_three_way` meet the constexpr iterator requirements ([[iterator.requirements.general]](iterator.requirements.general.md)) then the operations described in [Table 75](container.requirements.general.md#tab:container.opt) are implemented by constexpr functions.

<a id="tab:container.opt"></a>
**Table 75: Optional container operations** [tab:container.opt]

| Expression | Return type | Operational | Assertion/note | Complexity |
| --- | --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |  |
| `a <=> b` | `synth-three-way-result<value_type>` | `lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end(), synth-three-way)` | *Preconditions:* Either `<=>` is defined for values of type (possibly const) `T`, or `<` is defined for values of type (possibly const) `T` and `<` is a total ordering relationship. | linear |

[*Note 6*: The algorithm `lexicographical_compare_three_way` is defined in [[algorithms]](algorithms.md). — *end note*]

**15** All of the containers defined in this Clause and in [[basic.string]](basic.string.md) except `array` meet the additional requirements of an allocator-aware container, as described in [Table 76](container.requirements.general.md#tab:container.alloc.req).

Given an allocator type `A` and given a container type `X` having a `value_type` identical to `T` and an `allocator_type` identical to `allocator_traits<A>::rebind_alloc<T>` and given an lvalue `m` of type `A`, a pointer `p` of type `T*`, an expression `v` of type (possibly `const`) `T`, and an rvalue `rv` of type `T`, the following terms are defined. If `X` is not allocator-aware, the terms below are defined as if `A` were `allocator<T>` — no allocator object needs to be created and user specializations of `allocator<T>` are not instantiated:

- `T` is **Cpp17DefaultInsertable* into `X`* means that the following expression is well-formed:

  ```cpp
  allocator_traits<A>::construct(m, p)
  ```

- An element of `X` is *default-inserted* if it is initialized by evaluation of the expression

  ```cpp
  allocator_traits<A>::construct(m, p)
  ```

  where `p` is the address of the uninitialized storage for the element allocated within `X`.

- `T` is **Cpp17MoveInsertable* into `X`* means that the following expression is well-formed:

  ```cpp
  allocator_traits<A>::construct(m, p, rv)
  ```

  and its evaluation causes the following postcondition to hold: The value of `*p` is equivalent to the value of `rv` before the evaluation. [*Note 7*: `rv` remains a valid object. Its state is unspecified — *end note*]

- `T` is **Cpp17CopyInsertable* into `X`* means that, in addition to `T` being *Cpp17MoveInsertable* into `X`, the following expression is well-formed:

  ```cpp
  allocator_traits<A>::construct(m, p, v)
  ```

  and its evaluation causes the following postcondition to hold: The value of `v` is unchanged and is equivalent to `*p`.

- `T` is **Cpp17EmplaceConstructible* into `X` from `args`*, for zero or more arguments `args`, means that the following expression is well-formed:

  ```cpp
  allocator_traits<A>::construct(m, p, args)
  ```

- `T` is **Cpp17Erasable* from `X`* means that the following expression is well-formed:

  ```cpp
  allocator_traits<A>::destroy(m, p)
  ```

[*Note 8*: A container calls `allocator_traits<A>::construct(m, p, args)` to construct an element at `p` using `args`, with `m == get_allocator()`. The default `construct` in `allocator` will call `::new((void*)p) T(args)`, but specialized allocators may choose a different definition. — *end note*]

**16** In [Table 76](container.requirements.general.md#tab:container.alloc.req), `X` denotes an allocator-aware container class with a `value_type` of `T` using allocator of type `A`, `u` denotes a variable, `a` and `b` denote non-const lvalues of type `X`, `t` denotes an lvalue or a const rvalue of type `X`, `rv` denotes a non-const rvalue of type `X`, and `m` is a value of type `A`.

<a id="tab:container.alloc.req"></a>
**Table 76: Allocator-aware container requirements** [tab:container.alloc.req]

| Expression | Return type | Assertion/note | Complexity |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `allocator_type` | `A` | *Mandates:* `allocator_type::value_type` is the same as `X::value_type`. | compile time |
| `get_-` `allocator()` | `A` |  | constant |
| `X()`<br>`X u;` |  | *Preconditions:* `A` meets the *Cpp17DefaultConstructible* requirements.<br>*Postconditions:* `u.empty()` returns `true`, `u.get_allocator() == A()` | constant |
| `X(m)` |  | *Postconditions:* `u.empty()` returns `true`, | constant |
| `X u(m);` |  | `u.get_allocator() == m` |  |
| `X(t, m)`<br>`X u(t, m);` |  | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X`.<br>*Postconditions:* `u == t`, `u.get_allocator() == m` | linear |
| `X(rv)`<br>`X u(rv);` |  | *Postconditions:* `u` has the same elements as `rv` had before this construction; the value of `u.get_allocator()` is the same as the value of `rv.get_allocator()` before this construction. | constant |
| `X(rv, m)`<br>`X u(rv, m);` |  | *Preconditions:* `T` is *Cpp17MoveInsertable* into `X`.<br>*Postconditions:* `u` has the same elements, or copies of the elements, that `rv` had before this construction, `u.get_allocator() == m` | constant if `m ==` `rv.get_allocator()`, otherwise linear |
| `a = t` | `X&` | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*.<br>*Postconditions:* `a == t` | linear |
| `a = rv` | `X&` | *Preconditions:* If `allocator_-`<br>`traits<allocator_type>`<br>`::propagate_on_container_-`<br>`move_assignment::value` is<br>`false`, `T` is *Cpp17MoveInsertable* into `X` and *Cpp17MoveAssignable*.<br>*Effects:* All existing elements of `a` are either move assigned to or destroyed.<br>*Postconditions:* `a` is equal to the value that `rv` had before this assignment. | linear |
| `a.swap(b)` | `void` | *Effects:* exchanges the contents of `a` and `b` | constant |

**17** The behavior of certain container member functions and deduction guides depends on whether types qualify as input iterators or allocators. The extent to which an implementation determines that a type cannot be an input iterator is unspecified, except that as a minimum integral types shall not qualify as input iterators. Likewise, the extent to which an implementation determines that a type cannot be an allocator is unspecified, except that as a minimum a type `A` shall not qualify as an allocator unless it meets both of the following conditions:

- The *qualified-id* `A::value_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)).
- The expression `declval<A&>().allocate(size_t{})` is well-formed when treated as an unevaluated operand.
