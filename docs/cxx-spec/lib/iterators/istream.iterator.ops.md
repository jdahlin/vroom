# 23.6.1.2 Operations [istream.iterator.ops]

[Contents](../../README.md) · up: [23.6.1 Class template `istream_iterator` \[istream.iterator\]](istream.iterator.md) · prev: [23.6.1.1 Constructors and destructor \[istream.iterator.cons\]](istream.iterator.cons.md) · next: [23.6.2 Class template `ostream_iterator` \[ostream.iterator\]](ostream.iterator.md)

```cpp
const T& operator*() const;
```

**1** *Preconditions:* `in_stream != nullptr` is `true`.

**2** *Returns:* `value`.

```cpp
const T* operator->() const;
```

**3** *Preconditions:* `in_stream != nullptr` is `true`.

**4** *Returns:* `addressof(value)`.

```cpp
istream_iterator& operator++();
```

**5** *Preconditions:* `in_stream != nullptr` is `true`.

**6** *Effects:* Equivalent to:

```cpp
if (!(*in_stream >> value))
  in_stream = nullptr;
```

**7** *Returns:* `*this`.

```cpp
istream_iterator operator++(int);
```

**8** *Preconditions:* `in_stream != nullptr` is `true`.

**9** *Effects:* Equivalent to:

```cpp
istream_iterator tmp = *this;
++*this;
return tmp;
```

```cpp
template<class T, class charT, class traits, class Distance>
  bool operator==(const istream_iterator<T,charT,traits,Distance>& x,
                  const istream_iterator<T,charT,traits,Distance>& y);
```

**10** *Returns:* `x.in_stream == y.in_stream`.

```cpp
friend bool operator==(const istream_iterator& i, default_sentinel_t);
```

**11** *Returns:* `!i.in_stream`.
