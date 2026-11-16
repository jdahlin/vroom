# 24.7.3.1 Class template `ref_view` [range.ref.view]

[Contents](../README.md) · up: [24.7.3 All view \[range.all\]](range.all.md) · prev: [24.7.3 All view \[range.all\]](range.all.md) · next: [24.7.4 Filter view \[range.filter\]](range.filter.md)

**1** `ref_view` is a `view` of the elements of some other `range`.

```cpp
namespace std::ranges {
  template<range R>
    requires is_object_v<R>
  class ref_view : public view_interface<ref_view<R>> {
  private:
    R* r_ = nullptr;            // exposition only
  public:
    constexpr ref_view() noexcept = default;

    template<not-same-as<ref_view> T>
      requires see below
    constexpr ref_view(T&& t);

    constexpr R& base() const { return *r_; }

    constexpr iterator_t<R> begin() const { return ranges::begin(*r_); }
    constexpr sentinel_t<R> end() const { return ranges::end(*r_); }

    constexpr bool empty() const
      requires requires { ranges::empty(*r_); }
    { return ranges::empty(*r_); }

    constexpr auto size() const requires sized_range<R>
    { return ranges::size(*r_); }

    constexpr auto data() const requires contiguous_range<R>
    { return ranges::data(*r_); }
  };
  template<class R>
    ref_view(R&) -> ref_view<R>;
}
```

```cpp
template<not-same-as<ref_view> T>
  requires see below
constexpr ref_view(T&& t);
```

**2** *Remarks:* Let `FUN` denote the exposition-only functions

```cpp
void FUN(R&);
void FUN(R&&) = delete;
```

The expression in the *requires-clause* is equivalent to

```cpp
convertible_to<T, R&> && requires { FUN(declval<T>()); }
```

**3** *Effects:* Initializes `r_` with `addressof(static_cast<R&>(std::forward<T>(t)))`.
