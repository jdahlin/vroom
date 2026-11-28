/* bench/debug-info.py: every function's signature, as -g describes it, is clang's */
#include <stdarg.h>
#include <stddef.h>

typedef unsigned int u32;
typedef struct node {
    struct node *next;
    const char *name;
    u32 flags : 3, kind : 5;
    int : 4;
    long value[4][2];
    union {
        int i;
        float f;
    };
    struct {
        short a, b;
    } pair;
} node;
struct opaque;
enum color { RED, GREEN = 5, BLUE };
enum sign { MINUS = -1, PLUS = 1 };
struct flexible {
    int n;
    char data[];
};
typedef int (*callback)(void *, const char *, ...);

void nothing(void) {}
char c1(char a, signed char b, unsigned char c) { return a + b + c; }
short s1(unsigned short a, short b) { return a + b; }
long l1(unsigned long a, long long b, unsigned long long c) { return a + b + c; }
double d1(float a, double b, long double c) { return a + b + c; }
__int128 i128(unsigned __int128 a, _Bool b) { return a + b; }
struct node *walk(struct node *n, const volatile unsigned *p, int *restrict r) { return n; }
struct node by_value(node n) { return n; }
struct opaque *pass(struct opaque *o) { return o; }
enum color pick(enum sign s) { return RED; }
int flex(struct flexible *f) { return f->n; }
int call(callback cb, int (**table)(int), int (*matrix)[3][4]) { return 0; }
void (*get_handler(int sig, void (*handler)(int)))(int) { return handler; }
static int variadic(const char *fmt, ...) { return 0; }
int use(void) { return variadic("x"); }
_Complex float cf(_Complex double z) { return 0; }
unsigned long atomics(_Atomic int *a, const _Atomic long *b) { return 0; }
