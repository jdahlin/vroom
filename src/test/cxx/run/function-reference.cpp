// references to functions: a parameter, a variable, a return type, each called through
double h(double x) { return x * 2; }
double calld(double (&p)(double)) { return p(1.5); }
double (&ref())(double) { return h; }
int main() { double (&q)(double) = h; return calld(h) == 3 && q(2) == 4 && ref()(1) == 2 ? 0 : 1; }
