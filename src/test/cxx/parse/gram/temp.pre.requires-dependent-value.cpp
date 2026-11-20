// RUN: %clang_cc1 -std=c++20 %s
// [temp.pre]/9, [temp.res]/5: in a trailing requires-clause, a dependent
// qualified name without typename names a value, so the { that follows
// starts the function body and no braced-init-list of a type.
// (Found in libc++'s __atomic/atomic.h, atomic<T>::operator=.)
template <class T> struct base { static constexpr bool is_always_lock_free = true; };
template <class T> struct A : base<T> {
  using base_type = base<T>;
  T operator=(T d) volatile noexcept
    requires base_type::is_always_lock_free
  { return d; }
  T f(T d) requires T::value { return d; }
  T g(T d) requires base<T>::is_always_lock_free { return d; }
};
