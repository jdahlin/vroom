// [class.temporary]/2, [expr.ref]/6: members of an aggregate prvalue of a
// class with a destructor or reaching a base; a temporary at namespace scope.
static int destroyed;
struct D {
    int v;
    int get() const { return v; }
    ~D() { destroyed++; }
};
struct Base { int b; int getb() const { return b; } };
struct Derived : Base { int d; };

int via_destructor() {
    int r = D{5}.v + D{6}.get();
    return r;
}
int via_base() { return Derived{{3}, 4}.b + Derived{{7}, 1}.getb(); }

int main() {
    int a = via_destructor();
    int b = via_base();
    return a != 11 || destroyed != 2 || b != 10;
}
