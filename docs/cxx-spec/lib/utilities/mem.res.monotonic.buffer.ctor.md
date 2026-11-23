# 20.12.6.1 Constructors and destructor [mem.res.monotonic.buffer.ctor]

[Contents](../../README.md) · up: [20.12.6 Class `monotonic_buffer_resource` \[mem.res.monotonic.buffer\]](mem.res.monotonic.buffer.md) · prev: [20.12.6 Class `monotonic_buffer_resource` \[mem.res.monotonic.buffer\]](mem.res.monotonic.buffer.md) · next: [20.12.6.2 Members \[mem.res.monotonic.buffer.mem\]](mem.res.monotonic.buffer.mem.md)

```cpp
explicit monotonic_buffer_resource(memory_resource* upstream);
monotonic_buffer_resource(size_t initial_size, memory_resource* upstream);
```

**1** *Preconditions:* `upstream` is the address of a valid memory resource. `initial_size`, if specified, is greater than zero.

**2** *Effects:* Sets `upstream_rsrc` to `upstream` and `current_buffer` to `nullptr`. If `initial_size` is specified, sets `next_buffer_size` to at least `initial_size`; otherwise sets `next_buffer_size` to an implementation-defined size.

```cpp
monotonic_buffer_resource(void* buffer, size_t buffer_size, memory_resource* upstream);
```

**3** *Preconditions:* `upstream` is the address of a valid memory resource. `buffer_size` is no larger than the number of bytes in `buffer`.

**4** *Effects:* Sets `upstream_rsrc` to `upstream`, `current_buffer` to `buffer`, and `next_buffer_size` to `buffer_size` (but not less than 1), then increases `next_buffer_size` by an implementation-defined growth factor (which need not be integral).

```cpp
~monotonic_buffer_resource();
```

**5** *Effects:* Calls `release()`.
