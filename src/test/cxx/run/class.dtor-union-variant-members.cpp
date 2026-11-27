/* [class.dtor]/13: a union's destructor destroys none of its members (a
   template union of libc++'s variant shape, a named union and a union
   with a user-provided destructor) */
extern "C" int printf(const char *, ...);

struct A {
    int v = 1;
    ~A()
    {
        printf("A ");
    }
};
template <class T> struct Alt {
    T v;
};
union Named {
    char d;
    Alt<A> a;
    Named() : d(0) {}
    ~Named() {}
};
template <class... Ts> union Chain {
};
template <class T, class... Ts> union Chain<T, Ts...> {
    char d;
    Alt<T> head;
    Chain<Ts...> tail;
    Chain() : d(0) {}
    ~Chain() {}
};
struct Holder {
    Named named;
    Chain<A> chain;
};

int main()
{
    {
        Named u;
    }
    printf("| ");
    {
        Chain<int, A, A> v;
    }
    printf("| ");
    {
        Holder h;
    }
    printf("|\n");
    return 0;
}
