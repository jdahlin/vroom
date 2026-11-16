# C.4.11 [[containers]](../lib/containers/containers.md): containers library [diff.cpp03.containers]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.10 [[strings]](../lib/strings/strings.md): strings library \[diff.cpp03.strings\]](diff.cpp03.strings.md) · next: [C.4.12 [[algorithms]](../lib/algorithms/algorithms.md): algorithms library \[diff.cpp03.algorithms\]](diff.cpp03.algorithms.md)

**1** **Affected subclause:** [[container.requirements]](../lib/containers/container.requirements.md) <br>**Change:** Complexity of `size()` member functions now constant. <br>**Rationale:** Lack of specification of complexity of `size()` resulted in divergent implementations with inconsistent performance characteristics. <br>**Effect on original feature:** Some container implementations that conform to C++ 2003 may not conform to the specified `size()` requirements in this International Standard. Adjusting containers such as `std::list` to the stricter requirements may require incompatible changes.

**2** **Affected subclause:** [[container.requirements]](../lib/containers/container.requirements.md) <br>**Change:** Requirements change: relaxation. <br>**Rationale:** Clarification. <br>**Effect on original feature:** Valid C++ 2003 code that attempts to meet the specified container requirements may now be over-specified. Code that attempted to be portable across containers may need to be adjusted as follows:

- not all containers provide `size()`; use `empty()` instead of `size() == 0`;
- not all containers are empty after construction (`array`);
- not all containers have constant complexity for `swap()` (`array`).

**3** **Affected subclause:** [[container.requirements]](../lib/containers/container.requirements.md) <br>**Change:** Requirements change: default constructible. <br>**Rationale:** Clarification of container requirements. <br>**Effect on original feature:** Valid C++ 2003 code that attempts to explicitly instantiate a container using a user-defined type with no default constructor may fail to compile.

**4** **Affected subclauses:** [[sequence.reqmts]](../lib/containers/sequence.reqmts.md), [[associative.reqmts]](../lib/containers/associative.reqmts.md) <br>**Change:** Signature changes: from `void` return types. <br>**Rationale:** Old signature threw away useful information that may be expensive to recalculate. <br>**Effect on original feature:** The following member functions have changed:

- `erase(iter)` for `set`, `multiset`, `map`, `multimap`
- `erase(begin, end)` for `set`, `multiset`, `map`, `multimap`
- `insert(pos, num, val)` for `vector`, `deque`, `list`, `forward_list`
- `insert(pos, beg, end)` for `vector`, `deque`, `list`, `forward_list`

Valid C++ 2003 code that relies on these functions returning `void` (e.g., code that creates a pointer to member function that points to one of these functions) will fail to compile with this International Standard.

**5** **Affected subclauses:** [[sequence.reqmts]](../lib/containers/sequence.reqmts.md), [[associative.reqmts]](../lib/containers/associative.reqmts.md) <br>**Change:** Signature changes: from `iterator` to `const_iterator` parameters. <br>**Rationale:** Overspecification. <br>**Effect on original feature:** The signatures of the following member functions changed from taking an `iterator` to taking a `const_iterator`:

- `insert(iter, val)` for `vector`, `deque`, `list`, `set`, `multiset`, `map`, `multimap`
- `insert(pos, beg, end)` for `vector`, `deque`, `list`, `forward_list`
- `erase(begin, end)` for `set`, `multiset`, `map`, `multimap`
- all forms of `list::splice`
- all forms of `list::merge`

Valid C++ 2003 code that uses these functions may fail to compile with this International Standard.

**6** **Affected subclauses:** [[sequence.reqmts]](../lib/containers/sequence.reqmts.md), [[associative.reqmts]](../lib/containers/associative.reqmts.md) <br>**Change:** Signature changes: `resize`. <br>**Rationale:** Performance, compatibility with move semantics. <br>**Effect on original feature:** For `vector`, `deque`, and `list` the fill value passed to `resize` is now passed by reference instead of by value, and an additional overload of `resize` has been added. Valid C++ 2003 code that uses this function may fail to compile with this International Standard.
