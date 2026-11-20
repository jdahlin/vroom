/* [class.virtual]/16, [expr.call]/8: a virtual call whose object
   expression has side effects evaluates it once: the virtual table is
   read from, and the function called on, the same object. */
extern "C" int printf(const char *, ...);

struct Base {
    int id;
    virtual int get() const { return id; }
};
struct Twice : Base {
    int get() const override { return 2 * id; }
};

int calls = 0;
Base *pick(Base **all, int i)
{
    calls++;
    return all[i];
}

int main()
{
    Base a;
    a.id = 1;
    Twice b;
    b.id = 5;
    Base *all[2] = {&a, &b};
    int i = 0;
    int s = all[i++]->get();
    s += all[i++]->get();
    s += pick(all, 1)->get() + pick(all, 0)->get();
    printf("%d %d %d\n", s, i, calls);
    return s == 22 && i == 2 && calls == 2 ? 0 : 1;
}
