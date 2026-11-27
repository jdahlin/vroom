/* [class.copy.elision]/1.3: a member braced-initialized from a prvalue of
   its class is that object. */
extern "C" int printf(const char *, ...);
struct sp { int *p; sp(int*q=nullptr):p(q){} sp(sp&&o):p(o.p){o.p=nullptr; printf("move ");} ~sp(){ printf("dtor%d ", p!=nullptr);} };
int x = 5;
sp make() { return sp(&x); }
struct B { sp s; B() : s{make()} {} };
struct B2 { sp s; B2(sp &&o) : s{static_cast<sp&&>(o)} {} };
int main() { B b; printf("%d\n", *b.s.p); { B2 c(make()); printf("%d\n", *c.s.p);} }
