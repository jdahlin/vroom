/* vcc-flags: -fstrict-flex-arrays=3 */
#define FLEX_LEVEL 3
typedef __SIZE_TYPE__ size_t;
struct inner_bound { char fixed[3]; int end; char tail[2]; };
struct outer_bound { int start; struct inner_bound in[2]; char end[5]; };
struct one_bound { int n; char a[1]; };
struct many_bound { int n; char a[7]; };
static __attribute__((noinline)) int check(struct outer_bound *p, struct one_bound *o, struct many_bound *m)
{
    if (__builtin_object_size(p->in[1].tail,1) != 2) return 1;
    if (__builtin_object_size(p->in[1].tail,3) != 2) return 2;
    if (__builtin_object_size(p->in[1].tail+1,1) != 1) return 3;
    if (__builtin_object_size(p->in[1].tail+1,3) != 1) return 4;
    if (__builtin_object_size(p->in[0].fixed,1) != 3) return 5;
    if (__builtin_object_size(p->in[1].tail,0) != (size_t)-1) return 6;
    if (__builtin_object_size(p->in[1].tail,2) != 0) return 7;
    if (__builtin_object_size(o->a,1) != (FLEX_LEVEL < 2 ? (size_t)-1 : 1)) return 8;
    if (__builtin_object_size(m->a,1) != (FLEX_LEVEL == 0 ? (size_t)-1 : 7)) return 9;
    if (__builtin_object_size(o->a,3) != 1) return 10;
    if (__builtin_object_size(m->a,3) != 7) return 11;
    return 0;
}
int main(void)
{
    struct outer_bound p; struct one_bound o; struct many_bound m;
    if (__builtin_object_size(p.in[1].tail,1) != 2) return 12;
    if (__builtin_object_size(o.a,1) != 1) return 13;
    return check(&p,&o,&m);
}
