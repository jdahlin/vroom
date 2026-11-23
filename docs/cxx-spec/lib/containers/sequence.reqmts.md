# 22.2.3 Sequence containers [sequence.reqmts]

[Contents](../../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2.2 Container data races \[container.requirements.dataraces\]](container.requirements.dataraces.md) · next: [22.2.4 Node handles \[container.node\]](container.node.md)

**1** A sequence container organizes a finite set of objects, all of the same type, into a strictly linear arrangement. The library provides four basic kinds of sequence containers: `vector`, `forward_list`, `list`, and `deque`. In addition, `array` is provided as a sequence container which provides limited sequence operations because it has a fixed number of elements. The library also provides container adaptors that make it easy to construct abstract data types, such as `stack`s or `queue`s, out of the basic sequence container kinds (or out of other kinds of sequence containers that the user might define).

**2** [*Note 1*: The sequence containers offer the programmer different complexity trade-offs and should be used accordingly. `vector` is the type of sequence container that should be used by default. `array` should be used when the container has a fixed size known during translation. `list` or `forward_list` should be used when there are frequent insertions and deletions from the middle of the sequence. `deque` is the data structure of choice when most insertions and deletions take place at the beginning or at the end of the sequence. When choosing a container, remember `vector` is best; leave a comment to explain if you choose from the rest! — *end note*]

**3** In Tables [[tab:container.seq.req]](sequence.reqmts.md#tab:container.seq.req) and [[tab:container.seq.opt]](sequence.reqmts.md#tab:container.seq.opt), `X` denotes a sequence container class, `a` denotes a value of type `X` containing elements of type `T`, `u` denotes the name of a variable being declared, `A` denotes `X::allocator_type` if the *qualified-id* `X::allocator_type` is valid and denotes a type ([[temp.deduct]](../../temp/temp.deduct.md)) and `allocator<T>` if it doesn’t, `i` and `j` denote iterators that meet the *Cpp17InputIterator* requirements and refer to elements implicitly convertible to `value_type`, `[i, j)` denotes a valid range, `il` designates an object of type `initializer_list<value_type>`, `n` denotes a value of type `X::size_type`, `p` denotes a valid constant iterator to `a`, `q` denotes a valid dereferenceable constant iterator to `a`, `[q1, q2)` denotes a valid range of constant iterators in `a`, `t` denotes an lvalue or a const rvalue of `X::value_type`, and `rv` denotes a non-const rvalue of `X::value_type`. `Args` denotes a template parameter pack; `args` denotes a function parameter pack with the pattern `Args&&`.

**4** The complexities of the expressions are sequence dependent.

<a id="tab:container.seq.req"></a>
**Table 77: Sequence container requirements (in addition to container)** [tab:container.seq.req]

| Expression | Return type | Assertion/note |
| --- | --- | --- |
|  |  | pre-/post-condition |
| `X(n, t)`<br>`X u(n, t);` |  | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X`.<br>*Postconditions:* `distance(begin(), end()) == n`<br>*Effects:* Constructs a sequence container with `n` copies of `t` |
| `X(i, j)`<br>`X u(i, j);` |  | *Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `*i`. For `vector`, if the iterator does not meet the *Cpp17ForwardIterator* requirements ([[forward.iterators]](../iterators/forward.iterators.md)), `T` is also *Cpp17MoveInsertable* into `X`.<br>*Postconditions:* `distance(begin(), end()) ==` `distance(i, j)`<br>*Effects:* Constructs a sequence container equal to the range `[i, j)`. Each iterator in the range `[i, j)` is dereferenced exactly once. |
| `X(il)` |  | Equivalent to `X(il.begin(), il.end())` |
| `a = il` | `X&` | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*.<br>*Effects:* Assigns the range `[il.begin(), il.end())` into `a`. All existing elements of `a` are either assigned to or destroyed.<br>*Returns:* `*this`. |
| `a.emplace(p, args)` | `iterator` | *Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `args`. For `vector` and `deque`, `T` is also *Cpp17MoveInsertable* into `X` and *Cpp17MoveAssignable*.<br>*Effects:* Inserts an object of type `T` constructed with `std::forward<Args>(args)...` before `p`. [*Note 2*: `args` may directly or indirectly refer to a value in `a`. — *end note*] |
| `a.insert(p,t)` | `iterator` | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X`. For `vector` and `deque`, `T` is also *Cpp17CopyAssignable*.<br>*Effects:* Inserts a copy of `t` before `p`. |
| `a.insert(p,rv)` | `iterator` | *Preconditions:* `T` is *Cpp17MoveInsertable* into `X`. For `vector` and `deque`, `T` is also *Cpp17MoveAssignable*.<br>*Effects:* Inserts a copy of `rv` before `p`. |
| `a.insert(p,n,t)` | `iterator` | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*.<br>*Effects:* Inserts `n` copies of `t` before `p`. |
| `a.insert(p,i,j)` | `iterator` | *Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `*i`. For `vector` and `deque`, `T` is also *Cpp17MoveInsertable* into `X`, *Cpp17MoveConstructible*, *Cpp17MoveAssignable*, and swappable ([[swappable.requirements]](../library/swappable.requirements.md)). Neither `i` nor `j` are iterators into `a`.<br>*Effects:* Inserts copies of elements in `[i, j)` before `p`. Each iterator in the range `[i, j)` shall be dereferenced exactly once. |
| `a.insert(p, il)` | `iterator` | `a.insert(p, il.begin(), il.end())`. |
| `a.erase(q)` | `iterator` | *Preconditions:* For `vector` and `deque`, `T` is *Cpp17MoveAssignable*.<br>*Effects:* Erases the element pointed to by `q`. |
| `a.erase(q1,q2)` | `iterator` | *Preconditions:* For `vector` and `deque`, `T` is *Cpp17MoveAssignable*.<br>*Effects:* Erases the elements in the range `[q1, q2)`. |
| `a.clear()` | `void` | *Effects:* Destroys all elements in `a`. Invalidates all references, pointers, and iterators referring to the elements of `a` and may invalidate the past-the-end iterator.<br>*Postconditions:* `a.empty()` is `true`.<br>*Complexity:* Linear. |
| `a.assign(i,j)` | `void` | *Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `*i` and assignable from `*i`. For `vector`, if the iterator does not meet the forward iterator requirements ([[forward.iterators]](../iterators/forward.iterators.md)), `T` is also *Cpp17MoveInsertable* into `X`. Neither `i` nor `j` are iterators into `a`.<br>*Effects:* Replaces elements in `a` with a copy of `[i, j)`. Invalidates all references, pointers and iterators referring to the elements of `a`. For `vector` and `deque`, also invalidates the past-the-end iterator. Each iterator in the range `[i, j)` shall be dereferenced exactly once. |
| `a.assign(il)` | `void` | `a.assign(il.begin(), il.end())`. |
| `a.assign(n,t)` | `void` | *Preconditions:* `T` is *Cpp17CopyInsertable* into `X` and *Cpp17CopyAssignable*. `t` is not a reference into `a`.<br>*Effects:* Replaces elements in `a` with `n` copies of `t`. Invalidates all references, pointers and iterators referring to the elements of `a`. For `vector` and `deque`, also invalidates the past-the-end iterator. |

**5** The iterator returned from `a.insert(p, t)` points to the copy of `t` inserted into `a`.

**6** The iterator returned from `a.insert(p, rv)` points to the copy of `rv` inserted into `a`.

**7** The iterator returned from `a.insert(p, n, t)` points to the copy of the first element inserted into `a`, or `p` if `n == 0`.

**8** The iterator returned from `a.insert(p, i, j)` points to the copy of the first element inserted into `a`, or `p` if `i == j`.

**9** The iterator returned from `a.insert(p, il)` points to the copy of the first element inserted into `a`, or `p` if `il` is empty.

**10** The iterator returned from `a.emplace(p, args)` points to the new element constructed from `args` into `a`.

**11** The iterator returned from `a.erase(q)` points to the element immediately following `q` prior to the element being erased. If no such element exists, `a.end()` is returned.

**12** The iterator returned by `a.erase(q1, q2)` points to the element pointed to by `q2` prior to any elements being erased. If no such element exists, `a.end()` is returned.

**13** For every sequence container defined in this Clause and in [[strings]](../strings/strings.md):

- If the constructor

  ```cpp
  template<class InputIterator>
    X(InputIterator first, InputIterator last,
      const allocator_type& alloc = allocator_type());
  ```

  is called with a type `InputIterator` that does not qualify as an input iterator, then the constructor shall not participate in overload resolution.

- If the member functions of the forms:

  ```cpp
  template<class InputIterator>
    return-type F(const_iterator p,
                  InputIterator first, InputIterator last);       // such as insert

  template<class InputIterator>
    return-type F(InputIterator first, InputIterator last);       // such as append, assign

  template<class InputIterator>
    return-type F(const_iterator i1, const_iterator i2,
                  InputIterator first, InputIterator last);       // such as replace
  ```

  are called with a type `InputIterator` that does not qualify as an input iterator, then these functions shall not participate in overload resolution.

- A deduction guide for a sequence container shall not participate in overload resolution if it has an `InputIterator` template parameter and a type that does not qualify as an input iterator is deduced for that parameter, or if it has an `Allocator` template parameter and a type that does not qualify as an allocator is deduced for that parameter.

**14** [Table 78](sequence.reqmts.md#tab:container.seq.opt) lists operations that are provided for some types of sequence containers but not others. An implementation shall provide these operations for all container types shown in the “container” column, and shall implement them so as to take amortized constant time.

<a id="tab:container.seq.opt"></a>
**Table 78: Optional sequence container operations** [tab:container.seq.opt]

| Expression | Return type | Operational semantics | Container |
| --- | --- | --- | --- |
| `a.front()` | `reference; const_reference` for constant `a` | `*a.begin()` | `basic_string`, `array`, `deque`, `forward_list`, `list`, `vector` |
| `a.back()` | `reference; const_reference` for constant `a` | `{ auto tmp = a.end();`<br>`--tmp;`<br>`return *tmp; }` | `basic_string`, `array`, `deque`, `list`, `vector` |
| `a.emplace_front(args)` | `reference` | *Effects:* Prepends an object of type `T` constructed with `std::forward<Args>(args)...`.<br>*Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `args`.<br>*Returns:* `a.front()`. | `deque`, `forward_list`, `list` |
| `a.emplace_back(args)` | `reference` | *Effects:* Appends an object of type `T` constructed with `std::forward<Args>(args)...`.<br>*Preconditions:* `T` is *Cpp17EmplaceConstructible* into `X` from `args`. For `vector`, `T` is also *Cpp17MoveInsertable* into `X`.<br>*Returns:* `a.back()`. | `deque`, `list`, `vector` |
| `a.push_front(t)` | `void` | *Effects:* Prepends a copy of `t`.<br>*Preconditions:* `T` is *Cpp17CopyInsertable* into `X`. | `deque`, `forward_list`, `list` |
| `a.push_front(rv)` | `void` | *Effects:* Prepends a copy of `rv`.<br>*Preconditions:* `T` is *Cpp17MoveInsertable* into `X`. | `deque`, `forward_list`, `list` |
| `a.push_back(t)` | `void` | *Effects:* Appends a copy of `t`.<br>*Preconditions:* `T` is *Cpp17CopyInsertable* into `X`. | `basic_string`, `deque`, `list`, `vector` |
| `a.push_back(rv)` | `void` | *Effects:* Appends a copy of `rv`.<br>*Preconditions:* `T` is *Cpp17MoveInsertable* into `X`. | `basic_string`, `deque`, `list`, `vector` |
| `a.pop_front()` | `void` | *Effects:* Destroys the first element.<br>*Preconditions:* `a.empty()` is `false`. | `deque`, `forward_list`, `list` |
| `a.pop_back()` | `void` | *Effects:* Destroys the last element.<br>*Preconditions:* `a.empty()` is `false`. | `basic_string`, `deque`, `list`, `vector` |
| `a[n]` | `reference; const_reference` for constant `a` | `*(a.begin() + n)` | `basic_string`, `array`, `deque`, `vector` |
| `a.at(n)` | `reference; const_reference` for constant `a` | `*(a.begin() + n)` | `basic_string`, `array`, `deque`, `vector` |

**15** The member function `at()` provides bounds-checked access to container elements. `at()` throws `out_of_range` if `n >= a.size()`.
