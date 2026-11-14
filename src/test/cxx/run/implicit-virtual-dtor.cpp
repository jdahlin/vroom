// a polymorphic class with no destructor of its own whose members need destroying (json's parse_error:
// a std::exception with a std::runtime_error member): its implicit destructor overrides the base's in the
// vtable, so its objects are returned by value and destroyed through a base pointer
int freed = 0;
struct buf { int *p; buf(int v) : p(new int(v)) {} buf(const buf &o) : p(new int(*o.p)) {} ~buf() { delete p; freed++; } };
struct base { virtual ~base() {} virtual int what() const { return 0; } };
struct ex : base { buf m; int id; ex(int i) : m(i * 10), id(i) {} int what() const override { return *m.p; } };
struct pe : ex { pe(int i) : ex(i) {} static pe create(int i) { return pe(i); } };
int main() {
  { pe e = pe::create(3); if (e.id != 3 || e.what() != 30) return 1; }
  base *b = new pe(4); int w = b->what(); delete b;
  return w == 40 && freed >= 2 ? 0 : 1;
}
