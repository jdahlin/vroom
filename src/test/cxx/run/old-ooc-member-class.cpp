// a member class of a class template declared in it and defined outside (basic_ostream's sentry):
// each instance's from that definition, used in the template's members and outside; an explicit
// operator bool as ?:'s condition
template <class T> struct O {
  T v;
  class S;
  int get();
};
template <class T> class O<T>::S {
  O& o_;
public:
  explicit S(O& o) : o_(o) {}
  int val() const { return (int)o_.v + 1; }
  explicit operator bool() const { return o_.v != 0; }
};
template <class T> int O<T>::get() { S s(*this); return s.val(); }
int main() { O<int> o{4}; O<int>::S s(o); return (s ? s.val() : 0) == 5 && o.get() == 5 ? 0 : 1; }
