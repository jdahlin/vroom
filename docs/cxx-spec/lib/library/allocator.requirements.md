# 16.5.3.5 *Cpp17Allocator* requirements [allocator.requirements]

[Contents](../../README.md) · up: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · prev: [16.5.3.4 *Cpp17Hash* requirements \[hash.requirements\]](hash.requirements.md) · next: [16.5.3.5.1 Allocator completeness requirements \[allocator.requirements.completeness\]](allocator.requirements.completeness.md)

**1** The library describes a standard set of requirements for *allocators*, which are class-type objects that encapsulate the information about an allocation model. This information includes the knowledge of pointer types, the type of their difference, the type of the size of objects in this allocation model, as well as the memory allocation and deallocation primitives for it. All of the string types ([[strings]](../strings/strings.md)), containers ([[containers]](../containers/containers.md)) (except `array`), string buffers and string streams ([[input.output]](../io/input.output.md)), and `match_results` ([[re]](../re/re.md)) are parameterized in terms of allocators.

<a id="tab:allocator.req.var"></a>
**Table 35: Descriptive variable definitions** [tab:allocator.req.var]

| Variable | Definition |
| --- | --- |
| `T, U, C` | any *cv*-unqualified object type ([[basic.types]](../../basic/basic.types.md)) |
| `X` | an allocator class for type `T` |
| `Y` | the corresponding allocator class for type `U` |
| `XX` | the type `allocator_traits<X>` |
| `YY` | the type `allocator_traits<Y>` |
| `a, a1, a2` | lvalues of type `X` |
| `u` | the name of a variable being declared |
| `b` | a value of type `Y` |
| `c` | a pointer of type `C*` through which indirection is valid |
| `p` | a value of type `XX::pointer`, obtained by calling `a1.allocate`, where `a1 == a` |
| `q` | a value of type `XX::const_pointer` obtained by conversion from a value `p` |
| `r` | a value of type `T&` obtained by the expression `*p` |
| `w` | a value of type `XX::void_pointer` obtained by conversion from a value `p` |
| `x` | a value of type `XX::const_void_pointer` obtained by conversion from a value `q` or a value `w` |
| `y` | a value of type `XX::const_void_pointer` obtained by conversion from a result value of `YY::allocate`, or else a value of type (possibly `const`) `std::nullptr_t` |
| `n` | a value of type `XX::size_type` |
| `Args` | a template parameter pack |
| `args` | a function parameter pack with the pattern `Args&&` |

**2** The class template `allocator_traits` ([[allocator.traits]](../utilities/allocator.traits.md)) supplies a uniform interface to all allocator types. [Table 35](allocator.requirements.md#tab:allocator.req.var) describes the types manipulated through allocators. [Table 36](allocator.requirements.md#tab:cpp17.allocator) describes the requirements on allocator types and thus on types used to instantiate `allocator_traits`. A requirement is optional if the last column of [Table 36](allocator.requirements.md#tab:cpp17.allocator) specifies a default for a given expression. Within the standard library `allocator_traits` template, an optional requirement that is not supplied by an allocator is replaced by the specified default expression. A user specialization of `allocator_traits` may provide different defaults and may provide defaults for different requirements than the primary template. Within Tables [[tab:allocator.req.var]](allocator.requirements.md#tab:allocator.req.var) and [[tab:cpp17.allocator]](allocator.requirements.md#tab:cpp17.allocator), the use of `move` and `forward` always refers to `std::move` and `std::forward`, respectively.

<a id="tab:cpp17.allocator"></a>
**Table 36: *Cpp17Allocator* requirements** [tab:cpp17.allocator]

| Expression | Return type | Assertion/note | Default |
| --- | --- | --- | --- |
|  |  | pre-/post-condition |  |
| `X::pointer` |  |  | `T*` |
| `X::const_pointer` |  | `X::pointer` is convertible to `X::const_pointer` | `pointer_traits<X::pointer>::rebind<const T>` |
| `X::void_pointer`<br>`Y::void_pointer` |  | `X::pointer` is convertible to `X::void_pointer`. `X::void_pointer` and `Y::void_pointer` are the same type. | `pointer_traits<X::pointer>::rebind<void>` |
| `X::const_void_pointer`<br>`Y::const_void_pointer` |  | `X::pointer`, `X::const_pointer`, and `X::void_pointer` are convertible to `X::const_void_pointer`. `X::const_void_pointer` and `Y::const_void_pointer` are the same type. | `pointer_traits<X::pointer>::rebind<const void>` |
| `X::value_type` | Identical to `T` |  |  |
| `X::size_type` | unsigned integer type | a type that can represent the size of the largest object in the allocation model | `make_unsigned_t<X::difference_type>` |
| `X::difference_type` | signed integer type | a type that can represent the difference between any two pointers in the allocation model | `pointer_traits<X::pointer>::difference_type` |
| `typename X::template rebind<U>::other` | `Y` | For all `U` (including `T`), `Y::template rebind<T>::other` is `X`. | See Note A, below. |
| `*p` | `T&` |  |  |
| `*q` | `const T&` | `*q` refers to the same object as `*p`. |  |
| `p->m` | type of `T::m` | *Preconditions:* `(*p).m` is well-defined.<br>equivalent to `(*p).m` |  |
| `q->m` | type of `T::m` | *Preconditions:* `(*q).m` is well-defined.<br>equivalent to `(*q).m` |  |
| `static_cast<X::pointer>(w)` | `X::pointer` | `static_cast<X::pointer>(w) == p` |  |
| `static_cast<X::const_pointer>(x)` | `X::const_pointer` | `static_cast<` `X::const_pointer>(x) == q` |  |
| `pointer_traits<X::pointer>::pointer_to(r)` | `X::pointer` | same as `p` |  |
| `a.allocate(n)` | `X::pointer` | Memory is allocated for an array of `n` `T` and such an object is created but array elements are not constructed. [*Example 1*: When reusing storage denoted by some pointer value `p`, `launder(reinterpret_cast<T*>(new (p) byte[n * sizeof(T)]))` can be used to implicitly create a suitable array object and obtain a pointer to it. — *end example*] `allocate` may throw an appropriate exception.[^1] [*Note 1*: If `n == 0`, the return value is unspecified. — *end note*] |  |
| `a.allocate(n, y)` | `X::pointer` | Same as `a.allocate(n)`. The use of `y` is unspecified, but it is intended as an aid to locality. | `a.allocate(n)` |
| `a.deallocate(p,n)` | (not used) | *Preconditions:* `p` is a value returned by an earlier call to `allocate` that has not been invalidated by an intervening call to `deallocate`. `n` matches the value passed to `allocate` to obtain this memory.<br>*Throws:* Nothing. |  |
| `a.max_size()` | `X::size_type` | the largest value that can meaningfully be passed to `X::allocate()` | `numeric_limits<size_type>::max() / sizeof(value_type)` |
| `a1 == a2` | `bool` | Returns `true` only if storage allocated from each can be deallocated via the other. `operator==` shall be reflexive, symmetric, and transitive, and shall not exit via an exception. |  |
| `a1 != a2` | `bool` | same as `!(a1 == a2)` |  |
| `a == b` | `bool` | same as `a ==` `Y::rebind<T>::other(b)` |  |
| `a != b` | `bool` | same as `!(a == b)` |  |
| `X u(a)`; <br>`X u = a;` |  | Shall not exit via an exception.<br>*Postconditions:* `u == a` |  |
| `X u(b);` |  | Shall not exit via an exception.<br>*Postconditions:* `Y(u) == b`, `u == X(b)` |  |
| `X u(std::move(a));` <br>`X u = std::move(a);` |  | Shall not exit via an exception.<br>*Postconditions:* The value of `a` is unchanged and is equal to `u`. |  |
| `X u(std::move(b));` |  | Shall not exit via an exception.<br>*Postconditions:* `u` is equal to the prior value of `X(b)`. |  |
| `a.construct(c, args)` | (not used) | *Effects:* Constructs an object of type `C` at `c`. | `construct_at(c, std::forward<Args>(args)...)` |
| `a.destroy(c)` | (not used) | *Effects:* Destroys the object at `c` | `destroy_at(c)` |
| `a.select_on_container_copy_construction()` | `X` | Typically returns either `a` or `X()`. | `return a;` |
| `X::propagate_on_container_copy_assignment` | Identical to or derived from `true_type` or `false_type` | `true_type` only if an allocator of type `X` should be copied when the client container is copy-assigned. See Note B, below. | `false_type` |
| `X::propagate_on_container_move_assignment` | Identical to or derived from `true_type` or `false_type` | `true_type` only if an allocator of type `X` should be moved when the client container is move-assigned. See Note B, below. | `false_type` |
| `X::propagate_on_-` `container_swap` | Identical to or derived from `true_type` or `false_type` | `true_type` only if an allocator of type `X` should be swapped when the client container is swapped. See Note B, below. | `false_type` |
| `X::is_always_equal` | Identical to or derived from `true_type` or `false_type` | `true_type` only if the expression `a1 == a2` is guaranteed to be `true` for any two (possibly `const`) values `a1`, `a2` of type `X`. | `is_empty<X>::type` |

**3** Note A: The member class template `rebind` in the table above is effectively a typedef template. [*Note 2*: In general, if the name `Allocator` is bound to `SomeAllocator<T>`, then `Allocator::rebind<U>::other` is the same type as `SomeAllocator<U>`, where `SomeAllocator<T>::value_type` is `T` and `SomeAllocator<U>::value_type` is `U`. — *end note*] If `Allocator` is a class template instantiation of the form `SomeAllocator<T, Args>`, where `Args` is zero or more type arguments, and `Allocator` does not supply a `rebind` member template, the standard `allocator_traits` template uses `SomeAllocator<U, Args>` in place of `Allocator::rebind<U>::other` by default. For allocator types that are not template instantiations of the above form, no default is provided.

**4** Note B: If `X::propagate_on_container_copy_assignment::value` is `true`, `X` shall meet the *Cpp17CopyAssignable* requirements ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)) and the copy operation shall not throw exceptions. If `X::propagate_on_container_move_assignment::value` is `true`, `X` shall meet the *Cpp17MoveAssignable* requirements ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) and the move operation shall not throw exceptions. If `X::propagate_on_container_swap::value` is `true`, lvalues of type `X` shall be swappable ([[swappable.requirements]](swappable.requirements.md)) and the `swap` operation shall not throw exceptions.

**5** An allocator type `X` shall meet the *Cpp17CopyConstructible* requirements ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)). The `X::pointer`, `X::const_pointer`, `X::void_pointer`, and `X::const_void_pointer` types shall meet the *Cpp17NullablePointer* requirements ([Table 33](nullablepointer.requirements.md#tab:cpp17.nullablepointer)). No constructor, comparison function, copy operation, move operation, or swap operation on these pointer types shall exit via an exception. `X::pointer` and `X::const_pointer` shall also meet the requirements for a *Cpp17RandomAccessIterator* ([[random.access.iterators]](../iterators/random.access.iterators.md)) and the additional requirement that, when `a` and `(a + n)` are dereferenceable pointer values for some integral value `n`,

```cpp
addressof(*(a + n)) == addressof(*a) + n
```

is `true`.

**6** Let `x1` and `x2` denote objects of (possibly different) types `X::void_pointer`, `X::const_void_pointer`, `X::pointer`, or `X::const_pointer`. Then, `x1` and `x2` are *equivalently-valued* pointer values, if and only if both `x1` and `x2` can be explicitly converted to the two corresponding objects `px1` and `px2` of type `X::const_pointer`, using a sequence of `static_cast`s using only these four types, and the expression `px1 == px2` evaluates to `true`.

**7** Let `w1` and `w2` denote objects of type `X::void_pointer`. Then for the expressions

```cpp
w1 == w2
w1 != w2
```

either or both objects may be replaced by an equivalently-valued object of type `X::const_void_pointer` with no change in semantics.

**8** Let `p1` and `p2` denote objects of type `X::pointer`. Then for the expressions

```cpp
p1 == p2
p1 != p2
p1 < p2
p1 <= p2
p1 >= p2
p1 > p2
p1 - p2
```

either or both objects may be replaced by an equivalently-valued object of type `X::const_pointer` with no change in semantics.

**9** An allocator may constrain the types on which it can be instantiated and the arguments for which its `construct` or `destroy` members may be called. If a type cannot be used with a particular allocator, the allocator class or the call to `construct` or `destroy` may fail to instantiate.

**10** If the alignment associated with a specific over-aligned type is not supported by an allocator, instantiation of the allocator for that type may fail. The allocator also may silently ignore the requested alignment. [*Note 3*: Additionally, the member function `allocate` for that type may fail by throwing an object of type `bad_alloc`. — *end note*]

**11** [*Example 2*: The following is an allocator class template supporting the minimal interface that meets the requirements of [Table 36](allocator.requirements.md#tab:cpp17.allocator):

```cpp
template<class Tp>
struct SimpleAllocator {
  typedef Tp value_type;
  SimpleAllocator(ctor args);

  template<class T> SimpleAllocator(const SimpleAllocator<T>& other);

  [[nodiscard]] Tp* allocate(std::size_t n);
  void deallocate(Tp* p, std::size_t n);
};

template<class T, class U>
bool operator==(const SimpleAllocator<T>&, const SimpleAllocator<U>&);
template<class T, class U>
bool operator!=(const SimpleAllocator<T>&, const SimpleAllocator<U>&);
```

— *end example*]

## Subclauses

- [16.5.3.5.1 Allocator completeness requirements \[allocator.requirements.completeness\]](allocator.requirements.completeness.md)

[^1]: It is intended that `a.allocate` be an efficient means of allocating a single object of type `T`, even when `sizeof(T)` is small. That is, there is no need for a container to maintain its own free list.
