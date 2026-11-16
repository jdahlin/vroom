# D.16.1 Basic iterator [depr.iterator.basic]

[Contents](../README.md) · up: [D.16 Deprecated iterator primitives \[depr.iterator.primitives\]](depr.iterator.primitives.md) · prev: [D.16 Deprecated iterator primitives \[depr.iterator.primitives\]](depr.iterator.primitives.md) · next: [D.17 Deprecated `move_iterator` access \[depr.move.iter.elem\]](depr.move.iter.elem.md)

**1** The header `<iterator>` ([[iterator.synopsis]](../lib/iterators/iterator.synopsis.md)) has the following addition:

```cpp
namespace std {
  template<class Category, class T, class Distance = ptrdiff_t,
           class Pointer = T*, class Reference = T&>
  struct iterator {
    using iterator_category = Category;
    using value_type        = T;
    using difference_type   = Distance;
    using pointer           = Pointer;
    using reference         = Reference;
  };
}
```

**2** The `iterator` template may be used as a base class to ease the definition of required types for new iterators.

**3** [*Note 1*: If the new iterator type is a class template, then these aliases will not be visible from within the iterator class’s template definition, but only to callers of that class. — *end note*]

**4** [*Example 1*: If a C++ program wants to define a bidirectional iterator for some data structure containing `double` and such that it works on a large memory model of the implementation, it can do so with:

```cpp
class MyIterator :
  public iterator<bidirectional_iterator_tag, double, long, T*, T&> {
  // code implementing ++, etc.
};
```

— *end example*]
