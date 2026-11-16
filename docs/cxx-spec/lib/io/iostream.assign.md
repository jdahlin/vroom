# 29.7.4.6.3 Assignment and swap [iostream.assign]

[Contents](../../README.md) · up: [29.7.4.6 Class template `basic_iostream` \[iostreamclass\]](iostreamclass.md) · prev: [29.7.4.6.2 Destructor \[iostream.dest\]](iostream.dest.md) · next: [29.7.5 Output streams \[output.streams\]](output.streams.md)

```cpp
basic_iostream& operator=(basic_iostream&& rhs);
```

**1** *Effects:* Equivalent to: `swap(rhs)`.

```cpp
void swap(basic_iostream& rhs);
```

**2** *Effects:* Calls `basic_istream<charT, traits>::swap(rhs)`.
