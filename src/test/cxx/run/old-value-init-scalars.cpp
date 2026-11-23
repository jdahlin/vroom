// T() of a pointer typedef, int and long: zero (a null pointer)
template <class T> struct U { typedef T *pointer; pointer p; ~U() { reset(); } void reset() { pointer t = p; p = pointer(); (void)t; } };
typedef int I;
int main() { U<int> u; u.p = nullptr; int a = I(); long b = long(); return a + (int)b; }
