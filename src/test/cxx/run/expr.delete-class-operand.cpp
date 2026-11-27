// [expr.delete]/2: an operand of class type is converted by its conversion
// function to a pointer
static int gone;
struct obj {
    int v;
    virtual ~obj() { gone += v; }
};
template <class T> struct raw {
    T *p;
    operator T *() const { return p; }
    template <class U> explicit operator U *() const { return nullptr; }
};
struct arr {
    int *p;
    operator int *() { return p; }
};

int main()
{
    raw<obj> r{new obj};
    r.p->v = 5;
    delete r;
    arr a{new int[3]};
    delete[] a;
    __builtin_printf("%d\n", gone);
    return 0;
}
