/* cxx-run-flags: -frtti */
/* [abi:rtti]: a class's std::type_info object: _ZTI <type> of the library's
   __class_type_info (no base), __si_class_type_info (one public non-virtual
   base), or __vmi_class_type_info (flags, base count, each base with its
   offset and the virtual and public bits), its name _ZTS <type>, and the
   RTTI pointer of its virtual table.  Words are read by hand: the test
   links no library. */
namespace std {
class type_info {
public:
  bool operator==(const type_info &o) const { return this == &o; }
};
} // namespace std

extern "C" {
extern const void *const _ZTVN10__cxxabiv117__class_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv120__si_class_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv121__vmi_class_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv119__pointer_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv129__pointer_to_member_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv116__enum_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv120__function_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv117__array_type_infoE[4] = {};
extern const void *const _ZTIi[2] = {};
extern const void *const _ZTIPKi[2] = {};
}

struct A {
  virtual ~A();
  virtual void f();
  int a;
};
struct B : A {
  void f();
  int b;
};
struct C {
  virtual void g() {}
};
struct D : B, C {};
struct V : virtual A {};
struct E : V, C {};
struct P {
  int x;
};
struct Q : P {};
struct R : private P {};
struct S : P, Q {};
namespace {
struct L {
  virtual void h() {}
};
} // namespace
enum Color { red };
A::~A() {}
void A::f() {}
void B::f() {}

typedef const void *const *words;

static words info(const std::type_info &t) { return (words)&t; }

static bool same(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static long number(const void *p) { return (long)p; }

/* the type_info pointer a polymorphic object's virtual table holds */
static const void *rtti_of(const void *object)
{
  return ((words)*(const void *const *)object)[-1];
}

int main()
{
  D d;
  E e;
  A a;
  B b;
  C c;
  L l;
  /* the vtable's pointer */
  if (rtti_of(&a) != &typeid(A) || rtti_of(&b) != &typeid(B))
    return 1;
  if (rtti_of(&d) != &typeid(D) || rtti_of(&e) != &typeid(E))
    return 2;
  if (rtti_of(&c) != &typeid(C) || rtti_of(&l) != &typeid(L))
    return 3;
  /* the class of the object, the name */
  if (info(typeid(A))[0] != _ZTVN10__cxxabiv117__class_type_infoE + 2)
    return 4;
  if (info(typeid(B))[0] != _ZTVN10__cxxabiv120__si_class_type_infoE + 2)
    return 5;
  if (info(typeid(D))[0] != _ZTVN10__cxxabiv121__vmi_class_type_infoE + 2)
    return 6;
  if (!same((const char *)info(typeid(D))[1], "1D") ||
      !same((const char *)info(typeid(L))[1], "N12_GLOBAL__N_11LE"))
    return 7;
  /* __si_class_type_info: the base */
  if (info(typeid(B))[2] != &typeid(A))
    return 8;
  /* __vmi_class_type_info D: flags 0, two bases; B at 0, C at 16, public */
  words w = info(typeid(D));
  if (number(w[2]) != (2L << 32) || w[3] != &typeid(B) || number(w[4]) != 2 ||
      w[5] != &typeid(C) || number(w[6]) != ((16L << 8) | 2))
    return 9;
  /* E: V (non-virtual base with a virtual one) and C */
  w = info(typeid(E));
  if (number(w[2]) != (2L << 32) || w[3] != &typeid(V) || number(w[4]) != 2 ||
      w[5] != &typeid(C))
    return 10;
  /* V: the virtual base A, whose offset is in the virtual table's first
     slot before the offset to top: -24, virtual and public */
  w = info(typeid(V));
  if (number(w[2]) != (1L << 32) || w[3] != &typeid(A) ||
      number(w[4]) != ((-24L << 8) | 3))
    return 11;
  /* a private base is not public; a class repeating a base has a flag; a
     class with one non-dynamic base is single inheritance */
  if (info(typeid(Q))[0] != _ZTVN10__cxxabiv120__si_class_type_infoE + 2)
    return 12;
  w = info(typeid(R));
  if (info(typeid(R))[0] != _ZTVN10__cxxabiv121__vmi_class_type_infoE + 2 ||
      number(w[4]) != 0)
    return 13;
  w = info(typeid(S));
  if (number(w[2]) != ((1L) | (2L << 32)))
    return 14;
  /* pointers: flags (const 1, volatile 2) and the pointee */
  w = info(typeid(A *));
  if (w[0] != _ZTVN10__cxxabiv119__pointer_type_infoE + 2 ||
      number(w[2]) != 0 || w[3] != &typeid(A))
    return 15;
  w = info(typeid(const volatile B *));
  if (number(w[2]) != 3 || w[3] != &typeid(B))
    return 16;
  w = info(typeid(int A::*));
  if (w[0] != _ZTVN10__cxxabiv129__pointer_to_member_type_infoE + 2 ||
      w[3] != &typeid(int) || w[4] != &typeid(A))
    return 17;
  if (info(typeid(Color))[0] != _ZTVN10__cxxabiv116__enum_type_infoE + 2)
    return 18;
  if (info(typeid(void(int)))[0] !=
      _ZTVN10__cxxabiv120__function_type_infoE + 2)
    return 19;
  return 0;
}
