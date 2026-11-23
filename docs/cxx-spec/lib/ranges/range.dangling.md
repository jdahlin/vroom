# 24.5.4 Dangling iterator handling [range.dangling]

[Contents](../../README.md) · up: [24.5 Range utilities \[range.utility\]](range.utility.md) · prev: [24.5.3.2 Accessors \[range.subrange.access\]](range.subrange.access.md) · next: [24.6 Range factories \[range.factories\]](range.factories.md)

**1** The tag type `dangling` is used together with the template aliases `borrowed_iterator_t` and `borrowed_subrange_t` to indicate that an algorithm that typically returns an iterator into or subrange of a `range` argument does not return an iterator or subrange which could potentially reference a range whose lifetime has ended for a particular rvalue `range` argument which does not model `borrowed_range` ([[range.range]](range.range.md)).

```cpp
namespace std::ranges {
  struct dangling {
    constexpr dangling() noexcept = default;
    template<class... Args>
      constexpr dangling(Args&&...) noexcept { }
  };
}
```

**2** [*Example 1*:

```cpp
vector<int> f();
auto result1 = ranges::find(f(), 42);                                   // #1
static_assert(same_as<decltype(result1), ranges::dangling>);
auto vec = f();
auto result2 = ranges::find(vec, 42);                                   // #2
static_assert(same_as<decltype(result2), vector<int>::iterator>);
auto result3 = ranges::find(subrange{vec}, 42);                         // #3
static_assert(same_as<decltype(result3), vector<int>::iterator>);
```

The call to `ranges::find` at #1 returns `ranges::dangling` since `f()` is an rvalue `vector`; the `vector` could potentially be destroyed before a returned iterator is dereferenced. However, the calls at #2 and #3 both return iterators since the lvalue `vec` and specializations of `subrange` model `borrowed_range`. — *end example*]
