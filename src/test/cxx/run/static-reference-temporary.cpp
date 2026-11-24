/* [class.temporary]/6, [basic.stc.static]: a temporary bound to a reference
   at namespace scope has static storage duration (_ZGR<name>_), is
   destroyed at exit; a member of a prvalue in a namespace-scope initializer
   is a temporary of that initializer, destroyed with it */
extern "C" int printf(const char *, ...);
struct A {
    int a, b;
};
struct D {
    int a;
    ~D() { printf("~D%d\n", a); }
};
int f() { return 7; }
const A &r1 = A{f(), 2};
const D &r2 = D{f()};
int k = D{3}.a;
int m = A{f(), 4}.b;
int main()
{
    printf("%d %d %d %d %d\n", r1.a, r1.b, r2.a, k, m);
    return 0;
}
