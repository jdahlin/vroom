/* [class.base.init]/9, [dcl.init]/7.2: an array member of an anonymous struct
   whose elements have a default constructor is default-initialized element by
   element, also beside a mem-initializer of the struct */
void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }

struct N {
    int v;
    N() : v(5) {}
};

struct T {
    struct {
        N arr[2];
        int scalar[2] = {7, 8};
    };
    T() {}
};

struct U {
    struct {
        N one;
        N arr[2];
        int k;
    };
    U() : k(1) {}
};

alignas(T) static unsigned char t_storage[sizeof(T)];
alignas(U) static unsigned char u_storage[sizeof(U)];

int main()
{
    for (unsigned char &byte : t_storage)
        byte = 0xbe;
    for (unsigned char &byte : u_storage)
        byte = 0xbe;
    T *t = new (t_storage) T;
    U *u = new (u_storage) U;
    if (t->arr[0].v != 5 || t->arr[1].v != 5 || t->scalar[0] != 7 ||
        t->scalar[1] != 8)
        return 1;
    if (u->one.v != 5 || u->arr[0].v != 5 || u->arr[1].v != 5 || u->k != 1)
        return 2;
    return 0;
}
