/* [abi:pod]: a derived class's member in its base's tail padding; an
   assignment through a reference to the base copies the base's data size
   only (the base subobject is potentially overlapping) */
struct Base {
    int a;
    char b;
    Base() : a(0), b(0) {}
};
struct Derived : Base {
    char c;
};

void assign(Base &to, const Base &from) { to = from; }

int main()
{
    Derived d;
    d.c = 9;
    d.a = 1;
    d.b = 2;
    Base other;
    other.a = 5;
    other.b = 6;
    assign(d, other);
    int bad = 0;
    if (sizeof(Derived) != 8) bad |= 1;
    if (d.c != 9) bad |= 2;
    if (d.a != 5 || d.b != 6) bad |= 4;
    Base &r = d;
    if (r.a != 5) bad |= 8;
    return bad;
}
