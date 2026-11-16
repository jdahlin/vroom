# D.17 Deprecated `move_iterator` access [depr.move.iter.elem]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.16.1 Basic iterator \[depr.iterator.basic\]](depr.iterator.basic.md) · next: [D.18 Deprecated `shared_ptr` atomic access \[depr.util.smartptr.shared.atomic\]](depr.util.smartptr.shared.atomic.md)

**1** The following member is declared in addition to those members specified in [[move.iter.elem]](../lib/iterators/move.iter.elem.md):

```cpp
namespace std {
  template<class Iterator>
  class move_iterator {
  public:
    constexpr pointer operator->() const;
  };
}
```

```cpp
constexpr pointer operator->() const;
```

**2** *Returns:* `current`.
