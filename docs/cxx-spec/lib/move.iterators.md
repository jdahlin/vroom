# 23.5.3 Move iterators and sentinels [move.iterators]

[Contents](../README.md) · up: [23.5 Iterator adaptors \[predef.iterators\]](predef.iterators.md) · prev: [23.5.2.3.2 `inserter` \[inserter\]](inserter.md) · next: [23.5.3.1 Class template `move_iterator` \[move.iterator\]](move.iterator.md)

**1** Class template `move_iterator` is an iterator adaptor with the same behavior as the underlying iterator except that its indirection operator implicitly converts the value returned by the underlying iterator’s indirection operator to an rvalue. Some generic algorithms can be called with move iterators to replace copying with moving.

**2** [*Example 1*:

```cpp
list<string> s;
// populate the list s
vector<string> v1(s.begin(), s.end());          // copies strings into v1
vector<string> v2(make_move_iterator(s.begin()),
                  make_move_iterator(s.end())); // moves strings into v2
```

— *end example*]

## Subclauses

- [23.5.3.1 Class template `move_iterator` \[move.iterator\]](move.iterator.md)
- [23.5.3.2 Requirements \[move.iter.requirements\]](move.iter.requirements.md)
- [23.5.3.3 Construction and assignment \[move.iter.cons\]](move.iter.cons.md)
- [23.5.3.4 Conversion \[move.iter.op.conv\]](move.iter.op.conv.md)
- [23.5.3.5 Element access \[move.iter.elem\]](move.iter.elem.md)
- [23.5.3.6 Navigation \[move.iter.nav\]](move.iter.nav.md)
- [23.5.3.7 Comparisons \[move.iter.op.comp\]](move.iter.op.comp.md)
- [23.5.3.8 Non-member functions \[move.iter.nonmember\]](move.iter.nonmember.md)
- [23.5.3.9 Class template `move_sentinel` \[move.sentinel\]](move.sentinel.md)
- [23.5.3.10 Operations \[move.sent.ops\]](move.sent.ops.md)
