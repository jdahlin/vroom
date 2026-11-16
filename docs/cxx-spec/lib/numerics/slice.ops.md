# 26.7.4.4 Operators [slice.ops]

[Contents](../../README.md) · up: [26.7.4 Class `slice` \[class.slice\]](class.slice.md) · prev: [26.7.4.3 Access functions \[slice.access\]](slice.access.md) · next: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md)

```cpp
friend bool operator==(const slice& x, const slice& y);
```

**1** *Effects:* Equivalent to:

```cpp
return x.start() == y.start() && x.size() == y.size() && x.stride() == y.stride();
```
