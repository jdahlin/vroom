/* [temp.explicit]/2: an explicit instantiation declaration of member
   functions of a class template's specialization (libc++'s <string> has
   dozens): a reference return type, overloads, an operator, a qualified
   return type, a GNU attribute */
template <class C> struct B {
  typedef unsigned size_type;
  C value;
  B() : value() {}
  void init(const C *, size_type) {}
  B &assign(size_type n, C c)
  {
    value = c;
    return *this;
  }
  B &assign(const C *p)
  {
    value = *p;
    return *this;
  }
  B &operator=(C c)
  {
    value = c;
    return *this;
  }
  size_type find(C c, size_type) const
  {
    if (c == value)
      return 1;
    return 0;
  }
};
extern template void B<char>::init(const char *, unsigned);
extern template B<char> &B<char>::assign(unsigned, char);
extern template B<char> &B<char>::assign(const char *);
extern template B<char> &B<char>::operator=(char);
extern template B<char>::size_type B<char>::find(char, unsigned) const;
extern template __attribute__((__visibility__("default"))) void B<char>::init(const char *, unsigned);
int main()
{
  B<char> b;
  b.assign(1, 'x');
  b.operator=('y');
  if (b.find('y', 0) != 1)
    return 1;
  const char z = 'z';
  b.assign(&z);
  if (b.find('z', 0) != 1)
    return 2;
  return 0;
}
/* the definitions the declarations leave to another translation unit */
template B<char> &B<char>::assign(unsigned, char);
template B<char> &B<char>::assign(const char *);
template B<char> &B<char>::operator=(char);
template B<char>::size_type B<char>::find(char, unsigned) const;
