// B::operator=(x) of an empty base in a derived class's assignment (json's json_base_class_t): no bytes
// copied (the base's one byte is the derived class's first member)
template <class T> T &&mv(T &t) { return static_cast<T &&>(t); }
struct B {};
struct J : B {
  int k;
  J(int x = 0) : k(x) {}
  J &operator=(J other) { int t = k; k = other.k; other.k = t; B::operator=(mv(other)); return *this; }
};
int main() { J r; r = J(2); return r.k == 2 ? 0 : 1; }
