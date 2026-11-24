// [basic.stc.dynamic.deallocation]/2, [expr.delete]/6: a delete-expression
// whose class has a destroying operator delete calls it and runs no
// destructor first
extern "C" int printf(const char *, ...);
extern "C" void free(void *);
namespace std {
struct destroying_delete_t {
    explicit destroying_delete_t() = default;
};
} // namespace std
struct C {
    int v;
    ~C() { printf("~C\n"); }
    void operator delete(C *p, std::destroying_delete_t)
    {
        printf("destroying delete %d\n", p->v);
        p->~C();
        free(p);
    }
};
struct D {
    int v;
    ~D() { printf("~D\n"); }
    void operator delete(D *p, std::destroying_delete_t)
    {
        printf("no destructor %d\n", p->v);
        free(p);
    }
};
int main()
{
    C *p = new C;
    p->v = 7;
    delete p;
    D *q = new D;
    q->v = 8;
    delete q;
    delete static_cast<C *>(nullptr);
    printf("done\n");
}
