# 21.3.2.4 Capacity [string.capacity]

[Contents](../../README.md) · up: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · prev: [21.3.2.3 Iterator support \[string.iterators\]](string.iterators.md) · next: [21.3.2.5 Element access \[string.access\]](string.access.md)

```cpp
constexpr size_type size() const noexcept;
constexpr size_type length() const noexcept;
```

**1** *Returns:* A count of the number of char-like objects currently in the string.

**2** *Complexity:* Constant time.

```cpp
constexpr size_type max_size() const noexcept;
```

**3** *Returns:* The largest possible number of char-like objects that can be stored in a `basic_string`.

**4** *Complexity:* Constant time.

```cpp
constexpr void resize(size_type n, charT c);
```

**5** *Effects:* Alters the value of `*this` as follows:

- If `n <= size()`, erases the last `size() - n` elements.
- If `n > size()`, appends `n - size()` copies of `c`.

```cpp
constexpr void resize(size_type n);
```

**6** *Effects:* Equivalent to `resize(n, charT())`.

```cpp
constexpr size_type capacity() const noexcept;
```

**7** *Returns:* The size of the allocated storage in the string.

**8** *Complexity:* Constant time.

```cpp
constexpr void reserve(size_type res_arg);
```

**9** *Effects:* A directive that informs a `basic_string` of a planned change in size, so that the storage allocation can be managed accordingly. After `reserve()`, `capacity()` is greater or equal to the argument of `reserve` if reallocation happens; and equal to the previous value of `capacity()` otherwise. Reallocation happens at this point if and only if the current capacity is less than the argument of `reserve()`.

**10** *Throws:* `length_error` if `res_arg > max_size()` or any exceptions thrown by `allocator_traits` `<Allocator>::allocate`.

```cpp
constexpr void shrink_to_fit();
```

**11** *Effects:* `shrink_to_fit` is a non-binding request to reduce `capacity()` to `size()`. [*Note 1*: The request is non-binding to allow latitude for implementation-specific optimizations. — *end note*] It does not increase `capacity()`, but may reduce `capacity()` by causing reallocation.

**12** *Complexity:* If the size is not equal to the old capacity, linear in the size of the sequence; otherwise constant.

**13** *Remarks:* Reallocation invalidates all the references, pointers, and iterators referring to the elements in the sequence, as well as the past-the-end iterator. [*Note 2*: If no reallocation happens, they remain valid. — *end note*]

```cpp
constexpr void clear() noexcept;
```

**14** *Effects:* Equivalent to: `erase(begin(), end());`

```cpp
[[nodiscard]] constexpr bool empty() const noexcept;
```

**15** *Effects:* Equivalent to: `return size() == 0;`
