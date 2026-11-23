// [basic.scope.hiding]/2: a class hidden by a function or variable of its
// name in the same scope (struct stat and stat()), named by an
// elaborated-type-specifier; [basic.scope.pdecl]/7: class-key identifier ;
// declares a class in its own scope, an elaborated-type-specifier that finds
// none in the innermost namespace or block; [class.nest]/3: a nested class
// declared in its class and defined outside it
struct stat { int st_size; };
int stat(const char *path, struct stat *buf) { buf->st_size = 5; return 0; }
namespace N {
struct S { int a; };
int S = 3;
int size() { struct S s = { 4 }; return s.a + S; }
struct Fwd;
struct Fwd *fp;
struct Fwd { int z; };
}
struct Outer { int o; };
namespace M {
struct Outer;           // a new M::Outer
struct Outer *mo;
struct Outer { char c; };
int sz() { return sizeof(Outer); }
struct Unseen *up;      // declares M::Unseen
struct Unseen { int q; };
}
struct A { struct X { int v; }; int X; int get() { struct X x = { 2 }; return x.v + X; } };
struct Holder { struct In; struct In *p; };
struct Holder::In { int i; };
int main()
{
    struct stat sb;
    if (stat("x", &sb) || sb.st_size != 5) return 1;
    if (N::size() != 7) return 2;
    if (M::sz() != 1 || sizeof(Outer) != 4) return 3;
    struct L;
    struct L *lp = 0;
    struct L { int k; } l = { 6 };
    lp = &l;
    if (lp->k != 6) return 4;
    {
        struct Outer { long x; long y; };
        if (sizeof(struct Outer) != 16) return 5;
    }
    A a; a.X = 1;
    if (a.get() != 3) return 6;
    Holder::In in = { 8 };
    Holder h = { &in };
    if (h.p->i != 8) return 7;
    return 0;
}
