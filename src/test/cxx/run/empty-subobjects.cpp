/* [abi:class-types] II: an empty subobject that would share its address
   with another of its type moves (sizes as clang's for aarch64 Linux) */
struct E {};
struct F : E {};
struct A : E { E e; int x; };
struct B : E, F { int y; };
struct C { E e; F f; };
struct G : E { E a[3]; char c; };
struct H : F { E e; };

int main()
{
    int bad = 0;
    if (sizeof(A) != 8) bad |= 1;
    if (sizeof(B) != 4) bad |= 2;
    if (sizeof(C) != 2) bad |= 4;
    if (sizeof(G) != 5) bad |= 8;
    if (sizeof(H) != 2) bad |= 16;
    A a;
    a.x = 3;
    G g;
    g.c = 4;
    return bad + a.x + g.c - 7;
}
