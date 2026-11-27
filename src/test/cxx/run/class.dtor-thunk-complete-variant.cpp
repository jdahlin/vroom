/* [abi:mangling-special-thunks]: the complete object destructor's thunk of
   a destructor that is only made as the base object one. */
static int destroyed;
struct count {
    ~count() { destroyed++; }
};
struct a {
    virtual ~a() {}
    int x = 1;
};
struct b {
    virtual ~b() {}
    virtual int f() const = 0;
    int y = 2;
};
struct c : a, b {
    count n;
};
struct d : c {
    int f() const override { return x + y; }
};
int main()
{
    b *p = new d;
    int r = p->f();
    delete p;
    return r - 3 + destroyed - 1;
}
