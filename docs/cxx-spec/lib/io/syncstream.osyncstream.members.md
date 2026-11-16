# 29.10.3.3 Member functions [syncstream.osyncstream.members]

[Contents](../../README.md) · up: [29.10.3 Class template `basic_osyncstream` \[syncstream.osyncstream\]](syncstream.osyncstream.md) · prev: [29.10.3.2 Construction and destruction \[syncstream.osyncstream.cons\]](syncstream.osyncstream.cons.md) · next: [29.11 File systems \[filesystems\]](filesystems.md)

```cpp
void emit();
```

**1** *Effects:* Calls `sb.emit()`. If that call returns `false`, calls `setstate(ios_base::badbit)`.

**2** [*Example 1*: A flush on a `basic_osyncstream` does not flush immediately:

```cpp
{
  osyncstream bout(cout);
  bout << "Hello," << '\n';     // no flush
  bout.emit();                  // characters transferred; cout not flushed
  bout << "World!" << endl;     // flush noted; cout not flushed
  bout.emit();                  // characters transferred; cout flushed
  bout << "Greetings." << '\n'; // no flush
}   // characters transferred; cout not flushed
```

— *end example*]

**3** [*Example 2*: The function `emit()` can be used to handle exceptions from operations on the underlying stream.

```cpp
{
  osyncstream bout(cout);
  bout << "Hello, " << "World!" << '\n';
  try {
    bout.emit();
  } catch (...) {
    // handle exception
  }
}
```

— *end example*]

```cpp
streambuf_type* get_wrapped() const noexcept;
```

**4** *Returns:* `sb.get_wrapped()`.

**5** [*Example 3*: Obtaining the wrapped stream buffer with `get_wrapped()` allows wrapping it again with an `osyncstream`. For example,

```cpp
{
  osyncstream bout1(cout);
  bout1 << "Hello, ";
  {
    osyncstream(bout1.get_wrapped()) << "Goodbye, " << "Planet!" << '\n';
  }
  bout1 << "World!" << '\n';
}
```

produces the *uninterleaved* output

```text
Goodbye, Planet!
Hello, World!
```

— *end example*]
