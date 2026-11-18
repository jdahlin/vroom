/* [class.temporary]/4: a discarded prvalue of a class with a destructor
   is a temporary that the end of the full-expression destroys; that
   destruction is not lowered, so the expression is refused rather than
   compiled without the destructor's call. */
struct A {
    ~A() {}
};
struct B {
    ~B() {}
};
void f()
{
    A(), B();
}
