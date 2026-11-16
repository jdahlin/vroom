# 20.11.1.4 Creation [unique.ptr.create]

[Contents](../README.md) · up: [20.11.1 Class template `unique_ptr` \[unique.ptr\]](unique.ptr.md) · prev: [20.11.1.3.4 Modifiers \[unique.ptr.runtime.modifiers\]](unique.ptr.runtime.modifiers.md) · next: [20.11.1.5 Specialized algorithms \[unique.ptr.special\]](unique.ptr.special.md)

```cpp
template<class T, class... Args> unique_ptr<T> make_unique(Args&&... args);
```

**1** *Constraints:* `T` is not an array type.

**2** *Returns:* `unique_ptr<T>(new T(std::forward<Args>(args)...))`.

```cpp
template<class T> unique_ptr<T> make_unique(size_t n);
```

**3** *Constraints:* `T` is an array of unknown bound.

**4** *Returns:* `unique_ptr<T>(new remove_extent_t<T>[n]())`.

```cpp
template<class T, class... Args> unspecified make_unique(Args&&...) = delete;
```

**5** *Constraints:* `T` is an array of known bound.

```cpp
template<class T> unique_ptr<T> make_unique_for_overwrite();
```

**6** *Constraints:* `T` is not an array type.

**7** *Returns:* `unique_ptr<T>(new T)`.

```cpp
template<class T> unique_ptr<T> make_unique_for_overwrite(size_t n);
```

**8** *Constraints:* `T` is an array of unknown bound.

**9** *Returns:* `unique_ptr<T>(new remove_extent_t<T>[n])`.

```cpp
template<class T, class... Args> unspecified make_unique_for_overwrite(Args&&...) = delete;
```

**10** *Constraints:* `T` is an array of known bound.
