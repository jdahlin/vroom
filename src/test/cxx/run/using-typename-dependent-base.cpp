// using typename Base::name; in a class template: a member typedef of the dependent type
// (libc++'s __split_buffer re-exports its layout base's size_type, pointer, ... this way)
template <class T> struct layout { using size_type = unsigned long; using pointer = T *; pointer p; };
template <class T> class buffer : layout<T> {
  using base = layout<T>;
public:
  using typename base::pointer;
  using typename layout<T>::size_type;
  size_type size() { return sizeof(pointer); }
  void set(pointer q) { this->p = q; }
  pointer get() { return this->p; }
};
int main() {
  int x = 5;
  buffer<int> b;
  b.set(&x);
  return b.size() == sizeof(int *) && *b.get() == 5 ? 0 : 1;
}
