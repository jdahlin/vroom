# 29.10.2.3 Assignment and swap [syncstream.syncbuf.assign]

[Contents](../../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2.2 Construction and destruction \[syncstream.syncbuf.cons\]](syncstream.syncbuf.cons.md) · next: [29.10.2.4 Member functions \[syncstream.syncbuf.members\]](syncstream.syncbuf.members.md)

```cpp
basic_syncbuf& operator=(basic_syncbuf&& rhs) noexcept;
```

**1** *Effects:* Calls `emit()` then move assigns from `rhs`. After the move assignment `*this` has the observable state it would have had if it had been move constructed from `rhs` ([[syncstream.syncbuf.cons]](syncstream.syncbuf.cons.md)).

**2** *Returns:* `*this`.

**3** *Postconditions:*

- `rhs.get_wrapped() == nullptr` is `true`.

- `this->get_allocator() == rhs.get_allocator()` is `true` when

  ```cpp
  allocator_traits<Allocator>::propagate_on_container_move_assignment::value
  ```

  is `true`; otherwise, the allocator is unchanged.

**4** *Remarks:* This assignment operator disassociates `rhs` from its wrapped stream buffer, ensuring destruction of `rhs` produces no output.

```cpp
void swap(basic_syncbuf& other) noexcept;
```

**5** *Preconditions:* Either `allocator_traits<Allocator>::propagate_on_container_swap::value` is `true` or `this->get_allocator() == other.get_allocator()` is `true`.

**6** *Effects:* Exchanges the state of `*this` and `other`.
