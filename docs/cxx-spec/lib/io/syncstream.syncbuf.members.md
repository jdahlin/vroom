# 29.10.2.4 Member functions [syncstream.syncbuf.members]

[Contents](../../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2.3 Assignment and swap \[syncstream.syncbuf.assign\]](syncstream.syncbuf.assign.md) · next: [29.10.2.5 Overridden virtual functions \[syncstream.syncbuf.virtuals\]](syncstream.syncbuf.virtuals.md)

```cpp
bool emit();
```

**1** *Effects:* Atomically transfers the associated output of `*this` to the stream buffer `*wrapped`, so that it appears in the output stream as a contiguous sequence of characters. `wrapped->pubsync()` is called if and only if a call was made to `sync()` since the most recent call to `emit()`, if any.

**2** *Returns:* `true` if all of the following conditions hold; otherwise `false`:

- `wrapped == nullptr` is `false`.
- All of the characters in the associated output were successfully transferred.
- The call to `wrapped->pubsync()` (if any) succeeded.

**3** *Postconditions:* On success, the associated output is empty.

**4** *Synchronization:* All `emit()` calls transferring characters to the same stream buffer object appear to execute in a total order consistent with the “happens before” relation ([[intro.races]](../../basic/intro.races.md)), where each `emit()` call synchronizes with subsequent `emit()` calls in that total order.

**5** *Remarks:* May call member functions of `wrapped` while holding a lock uniquely associated with `wrapped`.

```cpp
streambuf_type* get_wrapped() const noexcept;
```

**6** *Returns:* `wrapped`.

```cpp
allocator_type get_allocator() const noexcept;
```

**7** *Returns:* A copy of the allocator that was set in the constructor or assignment operator.

```cpp
void set_emit_on_sync(bool b) noexcept;
```

**8** *Effects:* `emit_on_sync = b`.
