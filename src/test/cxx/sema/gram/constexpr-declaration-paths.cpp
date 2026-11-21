// [dcl.constexpr]/1: redeclarations must repeat constexpr in all scopes.
constexpr int valid(int);
constexpr int valid(int n) { return n; }
int block_valid(int n) { constexpr int valid(int); return valid(n); }
int block_without_constexpr(int n) { int valid(int); return valid(n); }
int ordinary(int);
int block_with_constexpr(int n) { constexpr int ordinary(int); return ordinary(n); }
int block_first(int n) { constexpr int later(int); return later(n); }
constexpr int later(int n) { return n; }
struct Member { constexpr int f(int) const; };
int Member::f(int n) const { return n; }
struct Reverse { int f(int) const; };
constexpr int Reverse::f(int n) const { return n; }
struct Constructor { int n; constexpr Constructor(int); };
Constructor::Constructor(int v) : n(v) {}
struct Destructor { constexpr ~Destructor(); };
Destructor::~Destructor() {}
struct NonConstexprDestructor { ~NonConstexprDestructor() {} };
struct ComposedDestructor {
    NonConstexprDestructor value;
    constexpr ~ComposedDestructor() {}
};
