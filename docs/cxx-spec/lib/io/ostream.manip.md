# 29.7.5.4 Standard manipulators [ostream.manip]

[Contents](../../README.md) · up: [29.7.5 Output streams \[output.streams\]](output.streams.md) · prev: [29.7.5.3 Unformatted output functions \[ostream.unformatted\]](ostream.unformatted.md) · next: [29.7.5.5 Rvalue stream insertion \[ostream.rvalue\]](ostream.rvalue.md)

**1** Each instantiation of any of the function templates specified in this subclause is a designated addressable function ([[namespace.std]](../library/namespace.std.md)).

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& endl(basic_ostream<charT, traits>& os);
```

**2** *Effects:* Calls `os.put(os.widen('\n'))`, then `os.flush()`.

**3** *Returns:* `os`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& ends(basic_ostream<charT, traits>& os);
```

**4** *Effects:* Inserts a null character into the output sequence: calls `os.put(charT())`.

**5** *Returns:* `os`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& flush(basic_ostream<charT, traits>& os);
```

**6** *Effects:* Calls `os.flush()`.

**7** *Returns:* `os`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& emit_on_flush(basic_ostream<charT, traits>& os);
```

**8** *Effects:* If `os.rdbuf()` is a `basic_syncbuf<charT, traits, Allocator>*`, called `buf` for the purpose of exposition, calls `buf->set_emit_on_sync(true)`. Otherwise this manipulator has no effect. [*Note 1*: To work around the issue that the `Allocator` template argument cannot be deduced, implementations can introduce an intermediate base class to `basic_syncbuf` that manages its `emit_on_sync` flag. — *end note*]

**9** *Returns:* `os`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& noemit_on_flush(basic_ostream<charT, traits>& os);
```

**10** *Effects:* If `os.rdbuf()` is a `basic_syncbuf<charT, traits, Allocator>*`, called `buf` for the purpose of exposition, calls `buf->set_emit_on_sync(false)`. Otherwise this manipulator has no effect.

**11** *Returns:* `os`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& flush_emit(basic_ostream<charT, traits>& os);
```

**12** *Effects:* Calls `os.flush()`. Then, if `os.rdbuf()` is a `basic_syncbuf<charT, traits, Allocator>*`, called `buf` for the purpose of exposition, calls `buf->emit()`.

**13** *Returns:* `os`.
