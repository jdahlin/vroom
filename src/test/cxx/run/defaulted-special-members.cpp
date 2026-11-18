/* libc++'s special members: [dcl.fct.def.default] explicit X() = default
   (is_execution_policy.h), X& operator=(const X&) = default
   (exception.h), a destructor's virt-specifier ([class.virtual]/4, /5),
   and [dcl.attr.grammar]/3's attribute-using-prefix
   (memory_resource.h) */
typedef unsigned long size_t;
/* the deleting destructors of the classes with virtual destructors */
void operator delete(void *) {}
void operator delete(void *, size_t) {}

struct Tag {
    explicit Tag() = default;
    int v;
};

struct Exception {
    int code;
    Exception() noexcept : code(1) {}
    Exception(const Exception &) noexcept = default;
    Exception &operator=(const Exception &) noexcept = default;
};

struct Base {
    int b = 0;
    virtual ~Base() {}
    virtual int f() { return 1; }
};
struct Derived : Base {
    int d = 0;
    ~Derived() override;
    int f() override { return 2; }
};
Derived::~Derived() {}
struct Last : Base {
    int l = 0;
    ~Last() final;
};
Last::~Last() {}

struct Resource {
    int unused;
    [[nodiscard]] [[using __gnu__: __returns_nonnull__, __alloc_size__(2),
                    __alloc_align__(3)]] void *
    allocate(int, int)
    {
        return this;
    }
    [[__gnu__::__nonnull__]] int count(int n) { return n; }
};

int main()
{
    Tag t;
    t.v = 3;
    Exception a, b;
    a.code = 7;
    b = a;
    Exception c(b);
    Derived d;
    Base *p = &d;
    Resource r;
    return t.v + c.code + p->f() + (r.allocate(1, 2) == &r) + r.count(1) - 14;
}
