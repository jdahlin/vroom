// explicit conversions by cast notation: [expr.cast]/4, [expr.const.cast]/3,
// [expr.static.cast]/13, [expr.reinterpret.cast]/4-8
int g = 5;

int f(int);
int main() {
    const char *msg = "hi";
    unsigned long a = (unsigned long)&g;
    int *p = (int *)a;
    char *q = (char *)msg;          // const_cast
    void *v = &g;
    int *w = (int *)v;              // static_cast from void*
    long *l = (long *)&g;           // reinterpret_cast
    char *c = (char *)(const int *)&g;  // reinterpret + const_cast
    void *fp = (void *)&f;          // function to object pointer
    int (*back)(int) = (int (*)(int))fp;
    bool b = (bool)p;
    return (*p == 5) + (q[0] == 'h') + (*w == 5) + (*(int *)l == 5) + (*(int *)c == 5) + (back == &f) + b;
}
int f(int x) { return x; }
