/* [func.require]/1.3, 1.7: INVOKE of a pointer to member on a class that is
   no object of the member's class dereferences it, (*t1).*f */
struct S { int v; int get() const { return v; } };
struct P { S *p; S &operator*() const { return *p; } };
int main() { S s{3}; P p{&s}; return __builtin_invoke(&S::get, p) + __builtin_invoke(&S::v, p) - 6; }
