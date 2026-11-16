# 29.10.2.2 Construction and destruction [syncstream.syncbuf.cons]

[Contents](../../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2.1 Overview \[syncstream.syncbuf.overview\]](syncstream.syncbuf.overview.md) · next: [29.10.2.3 Assignment and swap \[syncstream.syncbuf.assign\]](syncstream.syncbuf.assign.md)

```cpp
basic_syncbuf(streambuf_type* obuf, const Allocator& allocator);
```

**1** *Effects:* Sets `wrapped` to `obuf`.

**2** *Remarks:* A copy of `allocator` is used to allocate memory for internal buffers holding the associated output.

**3** *Throws:* Nothing unless an exception is thrown by the construction of a mutex or by memory allocation.

**4** *Postconditions:* `get_wrapped() == obuf` and `get_allocator() == allocator` are `true`.

```cpp
basic_syncbuf(basic_syncbuf&& other);
```

**5** *Postconditions:* The value returned by `this->get_wrapped()` is the value returned by `other.get_wrapped()` prior to calling this constructor. Output stored in `other` prior to calling this constructor will be stored in `*this` afterwards. `other.rdbuf()->pbase() == other.rdbuf()->pptr()` and `other.get_wrapped() == nullptr` are `true`.

**6** *Remarks:* This constructor disassociates `other` from its wrapped stream buffer, ensuring destruction of `other` produces no output.

```cpp
~basic_syncbuf();
```

**7** *Effects:* Calls `emit()`.

**8** *Throws:* Nothing. If an exception is thrown from `emit()`, the destructor catches and ignores that exception.
