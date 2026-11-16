// p->~T() of a scalar destroys nothing but evaluates p: (--e)->~T() decrements e (libc++'s valarray::__clear)
template <class T> int clear(T *b, T *e) { int n = 0; while (e != b) { (--e)->~T(); n++; if (n > 100) return -1; } return n; }
int main() { unsigned char a[3]; return clear(a, a + 3) == 3 ? 0 : 1; }
