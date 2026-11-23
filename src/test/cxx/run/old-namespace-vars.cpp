// also an unnamed namespace (no L in its names)
// variables of namespaces and static data members: their own mangled symbols (_ZN1n1xE, _ZN1mL1sE,
// _ZN1D3gotE), defined where the definition is; a static member function hiding a base's, called
// through a template
namespace n { int x = 3; int y; }
namespace m { static int s = 4; const int k = 9; int use() { return s + k; } }
namespace q { int x = 30; }
struct B { int k = 0; bool parse_error(int, const int &e) { k = e; return true; } };
struct D : B {
  static int got; static int *where; static const int lim = 7;
  static bool parse_error(int, const int &e) { got = e; where = &got; return false; }
};
int D::got = 0;
int *D::where = nullptr;
template <class S> bool run(S *s) { return s->parse_error(1, 5); }
int main() {
  D d; bool r = run(&d); n::y = 2;
  return !r && D::got == 5 && *D::where == 5 && d.k == 0 && n::x + n::y + q::x == 35 && m::use() == 13 && D::lim == 7 ? 0 : 1;
}
namespace { static int f2() { return 1; } static int v2 = 2; namespace in { static int z2 = 3; } }
namespace o { namespace { static int u2 = 4; } }
int unnamed() { return f2() + v2 + in::z2 + o::u2; }	// (no L in an unnamed namespace: _ZN12_GLOBAL__N_12v2E)
struct Str { static const long npos = -1; };
const long Str::npos;	// (a definition without the initializer: uses still see -1)
inline int counter = 5;	// (inline: a weak definition)

int more() { return Str::npos == -1 && counter == 5 ? 0 : 1; }
