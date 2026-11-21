// [expr.prim.id.qual]/3, [basic.stc.dynamic]: qualified operator-function-id
// (::operator new, ::operator delete, ns::operator+).
void *operator new(unsigned long);
void operator delete(void *) noexcept;

namespace ops {
struct Box {
    int v;
};
int operator+(Box a, Box b) { return a.v + b.v; }
}

int main() {
    void *p = ::operator new(sizeof(int));
    ::operator delete(p);
    ops::Box a{3}, b{4};
    return ops::operator+(a, b) != 7;
}
