/* [dcl.init]/17.6.2.1: T({...}) of an aggregate that is not trivially
   copyable; the move is elided */
extern "C" int printf(const char *, ...);

struct E {
    int v;
    E(int v) : v(v) {}
    E(E &&o) : v(o.v) { printf("move\n"); }
    ~E() { printf("destroy %d\n", v); }
};
struct A {
    E e[2];
};
struct B {
    E x;
    E y;
};

int main()
{
    A a({1, 2});
    B b({3, 4});
    A c = A({5, 6});
    return a.e[1].v + b.y.v + c.e[0].v - 11;
}
