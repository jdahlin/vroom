# 20.11.6 Class template `enable_shared_from_this` [util.smartptr.enab]

[Contents](../../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.5 Class template `owner_less` \[util.smartptr.ownerless\]](util.smartptr.ownerless.md) · next: [20.11.7 Smart pointer hash support \[util.smartptr.hash\]](util.smartptr.hash.md)

**1** A class `T` can inherit from `enable_shared_from_this<T>` to inherit the `shared_from_this` member functions that obtain a `shared_ptr` instance pointing to `*this`.

**2** [*Example 1*:

```cpp
struct X: public enable_shared_from_this<X> { };

int main() {
  shared_ptr<X> p(new X);
  shared_ptr<X> q = p->shared_from_this();
  assert(p == q);
  assert(!p.owner_before(q) && !q.owner_before(p)); // p and q share ownership
}
```

— *end example*]

```cpp
namespace std {
  template<class T> class enable_shared_from_this {
  protected:
    constexpr enable_shared_from_this() noexcept;
    enable_shared_from_this(const enable_shared_from_this&) noexcept;
    enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept;
    ~enable_shared_from_this();

  public:
    shared_ptr<T> shared_from_this();
    shared_ptr<T const> shared_from_this() const;
    weak_ptr<T> weak_from_this() noexcept;
    weak_ptr<T const> weak_from_this() const noexcept;

  private:
    mutable weak_ptr<T> weak_this;  // exposition only
  };
}
```

**3** The template parameter `T` of `enable_shared_from_this` may be an incomplete type.

```cpp
constexpr enable_shared_from_this() noexcept;
enable_shared_from_this(const enable_shared_from_this<T>&) noexcept;
```

**4** *Effects:* Value-initializes `weak_this`.

```cpp
enable_shared_from_this<T>& operator=(const enable_shared_from_this<T>&) noexcept;
```

**5** *Returns:* `*this`.

**6** [*Note 1*: `weak_this` is not changed. — *end note*]

```cpp
shared_ptr<T>       shared_from_this();
shared_ptr<T const> shared_from_this() const;
```

**7** *Returns:* `shared_ptr<T>(weak_this)`.

```cpp
weak_ptr<T>       weak_from_this() noexcept;
weak_ptr<T const> weak_from_this() const noexcept;
```

**8** *Returns:* `weak_this`.
