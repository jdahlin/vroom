/* [class.abstract]/2: a pure virtual destructor, defined outside. */
extern "C" int printf(const char *, ...);
void operator delete(void *, unsigned long) noexcept {}
struct B {
    virtual ~B() = 0;
    virtual int f() { return 1; }
};
B::~B() { printf("~B\n"); }
struct D : B {
    ~D() override { printf("~D\n"); }
    int f() override { return 2; }
};
struct E : B {};
int main()
{
    D d;
    E e;
    B *p = &d, *q = &e;
    printf("%d %d %d\n", p->f(), q->f(), static_cast<int>(__is_abstract(B)));
}
