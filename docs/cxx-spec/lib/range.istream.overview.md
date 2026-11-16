# 24.6.4.1 Overview [range.istream.overview]

[Contents](../README.md) · up: [24.6.4 Istream view \[range.istream\]](range.istream.md) · prev: [24.6.4 Istream view \[range.istream\]](range.istream.md) · next: [24.6.4.2 Class template `basic_istream_view` \[range.istream.view\]](range.istream.view.md)

**1** `basic_istream_view` models `input_range` and reads (using `operator>>`) successive elements from its corresponding input stream.

**2** [*Example 1*:

```cpp
auto ints = istringstream{"0 1  2   3     4"};
ranges::copy(istream_view<int>(ints), ostream_iterator<int>{cout, "-"});
// prints 0-1-2-3-4-
```

— *end example*]
