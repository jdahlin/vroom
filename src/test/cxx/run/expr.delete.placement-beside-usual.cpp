// [basic.stc.dynamic.deallocation]/3, [expr.delete]/10: a placement member
// operator delete beside a usual member operator delete(void *) is ignored
// when selecting the usual deallocation function for a virtual destructor.

static int deleted_count = 0;

struct Base {
    Base() = default;
    virtual ~Base() {}
    void *operator new(decltype(sizeof(0)), void *p, decltype(sizeof(0)))
    {
        return p;
    }
    void operator delete(void *, void *, decltype(sizeof(0))) {}
    void operator delete(void *p)
    {
        deleted_count++;
        ::operator delete(p);
    }
    virtual int id() const { return 1; }
};

struct Derived : Base {
    int id() const override { return 2; }
};

int main()
{
    Derived d;
    if (d.id() != 2)
        return 1;
    return 0;
}
