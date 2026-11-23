// a class template's nested class whose destructor only the outer destructor runs (implicitly): its
// body parsed before the outer one's, for each instance (basic_json<..., long double>::data::~data)
int destroyed = 0;
template <class T> struct J {
  struct data { T v; ~data() { destroyed += (int)sizeof(T); } };
  data d;
  ~J() { d.v = 0; }
};
int main() {
  { J<double> a; J<long double> b; J<char> c; a.d.v = 1; b.d.v = 2; c.d.v = 3; }
  return destroyed == (int)(sizeof(double) + sizeof(long double) + 1) ? 0 : 1;
}
