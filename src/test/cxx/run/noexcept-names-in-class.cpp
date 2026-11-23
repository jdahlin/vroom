/* [except.spec]/2, [class.mem]/6: a member function's noexcept-specifier
   naming a namespace-scope template that other members' specifiers name too
   (libc++'s basic_string: noexcept(__noexcept_move_assign_container<...>::
   value) twice).  A name used with < or :: is not a member's declaration. */
template <class T> struct trait {
  static const bool value = sizeof(T) <= 4;
};

template <class T> struct box {
  T v;
  box &operator=(box &&o) noexcept(trait<T>::value)
  {
    v = o.v;
    return *this;
  }
  void swap(box &o) noexcept(trait<T>::value)
  {
    T t = v;
    v = o.v;
    o.v = t;
  }
};

int main()
{
  box<int> a = {1}, b = {2};
  a.swap(b);
  if (a.v != 2 || b.v != 1)
    return 1;
  if (!noexcept(a.swap(b)))
    return 2;
  box<long long> c = {3}, d = {4};
  if (noexcept(c.swap(d)))
    return 3;
  return 0;
}
