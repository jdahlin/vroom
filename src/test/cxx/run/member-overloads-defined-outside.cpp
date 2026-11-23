/* [temp.mem.func]/1: an overloaded member function of a class template,
   each overload defined outside the class (libc++'s slice_array::operator=:
   four overloads, three defined after the class), and a const local that a
   lambda names, initialized from a parameter ([expr.const]/5.8: not usable
   in constant expressions, so the lambda captures it) */
template <class T> struct holder {
  T value;
  int set(const holder &h) const;
  int set(const T &x) const;
  int set(long n) const;
};
template <class T> int holder<T>::set(const holder &h) const { return h.value + 1; }
template <class T> int holder<T>::set(const T &x) const { return x + 2; }
template <class T> int holder<T>::set(long n) const { return int(n) + 3; }

struct counted {
  unsigned long n;
  constexpr unsigned long size() const { return n; }
};
unsigned long total;
void add(unsigned long k) { total += k; }
template <class T> void run(counted c, const holder<T> &h)
{
  const unsigned long n = c.size();
  const unsigned long m = h.value;
  auto each = [&] { add(n); add(m); };
  each();
  each();
}

int main()
{
  holder<int> h = {10};
  if (h.set(h) != 11 || h.set(5) != 7 || h.set(5L) != 8)
    return 1;
  run(counted{3}, h);
  if (total != 26)
    return 2;
  return 0;
}
