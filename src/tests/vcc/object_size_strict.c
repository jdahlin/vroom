/* vcc-flags: -fstrict-flex-arrays=3 */
typedef __SIZE_TYPE__ size_t;
struct fixed { int n; char a[9]; };
struct one { int n; char a[1]; };
struct zero { int n; char a[0]; };
struct flex { int n; char a[]; };
static __attribute__((noinline)) int check(struct fixed *p, struct one *q,
                                         struct zero *r, struct flex *s)
{
    if (__builtin_object_size(p->a, 1) != 9) return 1;
    if (__builtin_object_size(p->a + 2, 1) != 7) return 2;
    if (__builtin_object_size(p->a, 0) != (size_t)-1) return 3;
    if (__builtin_object_size(p->a, 2) != 0) return 4;
    if (__builtin_object_size(p->a, 3) != 9) return 5;
    if (__builtin_object_size(q->a, 1) != 1) return 6;
    if (__builtin_object_size(r->a, 1) != 0) return 7;
    if (__builtin_object_size(s->a, 1) != (size_t)-1) return 8;
    struct fixed *saved = p;
    if (__builtin_object_size((p++)->a, 1) != 9) return 9;
    if (p != saved) return 10;
    return 0;
}
int main(void)
{
    struct fixed p = {0}; struct one q = {0}; struct zero r = {0};
    struct flex s = {0};
    return check(&p, &q, &r, &s);
}
