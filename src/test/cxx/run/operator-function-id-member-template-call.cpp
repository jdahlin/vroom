// [over.call], [temp.over]: an operator function's name called as a function in a member of its
// class, the name of member functions and member function templates (json's operator[](T* key)
// returning operator[](typename object_t::key_type(key)))
struct S {
  int v[3] = {1, 2, 3};
  int &operator[](int i) { return v[i]; }
  template <class T> int &operator[](T *p) { return operator[](int(*p)); }
  int &get(int i) { return operator[](i); }
  int &get(long *p) { return this->operator[](p); }
};
int main() {
  S s;
  int k = 2;
  long l = 0;
  return s[&k] == 3 && s.get(1) == 2 && s.get(&l) == 1 ? 0 : 1;
}
