/* [conv]/1, [over.best.ics]/4: no standard conversion takes a class type to
   a pointer, so a constructor taking a pointer is not viable for a class
   argument and overload resolution chooses the other one */
struct A {
    int x;
};
struct B {
    int which;
    B(const char *) : which(1) {}
    B(const A &) : which(2) {}
};
struct C {
    int which;
    C(int *) : which(1) {}
    C(const B &) : which(2) {}
};
int main()
{
    A a = {1};
    B b(a);
    C c(b);
    if (b.which != 2 || c.which != 2)
        return 1;
    return 0;
}
