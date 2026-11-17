int g = 3;
static int sg;
extern int eg;
const int cg = 4;
void f0(void) {}
int f1(int a, const char *p, const char *q) { return a; }
static long f2(unsigned long, long long, short *, short *) { return 0; }
int f3(int (*fp)(int), int (*gp)(int), int a[], int (&r)[3], ...) { return 0; }
void f4(char, signed char, unsigned char, bool, wchar_t, char16_t, char32_t, float, double, long double, decltype(nullptr)) {}
int f5() {
    static int s = 1;
    { static int s = 2; }
    { static int s = 3; static int t; }
    return s;
}
void f6(int *const p, volatile int *v, const volatile int **w, int &&rr) {}
int main() { return f2(0,0,0,0); }
