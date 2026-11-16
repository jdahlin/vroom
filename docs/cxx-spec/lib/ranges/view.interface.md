# 24.5.2 View interface [view.interface]

[Contents](../../README.md) · up: [24.5 Range utilities \[range.utility\]](range.utility.md) · prev: [24.5.1 Helper concepts \[range.utility.helpers\]](range.utility.helpers.md) · next: [24.5.2.1 Members \[view.interface.members\]](view.interface.members.md)

**1** The class template `view_interface` is a helper for defining `view`-like types that offer a container-like interface. It is parameterized with the type that is derived from it.

```cpp
namespace std::ranges {
  template<class D>
    requires is_class_v<D> && same_as<D, remove_cv_t<D>>
  class view_interface : public view_base {
  private:
    constexpr D& derived() noexcept {                   // exposition only
      return static_cast<D&>(*this);
    }
    constexpr const D& derived() const noexcept {       // exposition only
      return static_cast<const D&>(*this);
    }
  public:
    constexpr bool empty() requires forward_range<D> {
      return ranges::begin(derived()) == ranges::end(derived());
    }
    constexpr bool empty() const requires forward_range<const D> {
      return ranges::begin(derived()) == ranges::end(derived());
    }

    constexpr explicit operator bool()
      requires requires { ranges::empty(derived()); } {
        return !ranges::empty(derived());
      }
    constexpr explicit operator bool() const
      requires requires { ranges::empty(derived()); } {
        return !ranges::empty(derived());
      }

    constexpr auto data() requires contiguous_iterator<iterator_t<D>> {
      return to_address(ranges::begin(derived()));
    }
    constexpr auto data() const
      requires range<const D> && contiguous_iterator<iterator_t<const D>> {
        return to_address(ranges::begin(derived()));
      }

    constexpr auto size() requires forward_range<D> &&
      sized_sentinel_for<sentinel_t<D>, iterator_t<D>> {
        return ranges::end(derived()) - ranges::begin(derived());
      }
    constexpr auto size() const requires forward_range<const D> &&
      sized_sentinel_for<sentinel_t<const D>, iterator_t<const D>> {
        return ranges::end(derived()) - ranges::begin(derived());
      }

    constexpr decltype(auto) front() requires forward_range<D>;
    constexpr decltype(auto) front() const requires forward_range<const D>;

    constexpr decltype(auto) back() requires bidirectional_range<D> && common_range<D>;
    constexpr decltype(auto) back() const
      requires bidirectional_range<const D> && common_range<const D>;

    template<random_access_range R = D>
      constexpr decltype(auto) operator[](range_difference_t<R> n) {
        return ranges::begin(derived())[n];
      }
    template<random_access_range R = const D>
      constexpr decltype(auto) operator[](range_difference_t<R> n) const {
        return ranges::begin(derived())[n];
      }
  };
}
```

**2** The template parameter `D` for `view_interface` may be an incomplete type. Before any member of the resulting specialization of `view_interface` other than special member functions is referenced, `D` shall be complete, and model both `derived_from<view_interface<D>>` and `view`.

## Subclauses

- [24.5.2.1 Members \[view.interface.members\]](view.interface.members.md)
