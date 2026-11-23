// __builtin_get_vtable_pointer: the pointer stored at the object's start, equal for objects of one class
struct P {
    virtual int f() { return 1; }
};
struct Q : P {
    int f() override { return 2; }
};
const void *vt(P *x) { return __builtin_get_vtable_pointer(x); }
int main()
{
    P a, b;
    Q c;
    P array[2];
    if (vt(&a) != vt(&b) || vt(&a) == vt(&c) || !vt(&c) || vt(array) != vt(&a))
        return 1;
    return 0;
}
