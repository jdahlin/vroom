typedef __SIZE_TYPE__ size_t;
struct counted_volatile { int n; int *volatile data __attribute__((counted_by(n))); };
struct counted_plain { int n; int *data __attribute__((counted_by(n))); };
static struct counted_plain *volatile base;
static __attribute__((noinline)) size_t field(struct counted_volatile *p)
{ return __builtin_dynamic_object_size(p->data,0); }
static __attribute__((noinline)) size_t pointer(void)
{ return __builtin_dynamic_object_size(base->data,0); }
int main(void)
{
    /* These queries stay unknown and must not dereference the null base. */
    if (field((void *)0) != (size_t)-1) return 1;
    if (pointer() != (size_t)-1) return 2;
    return 0;
}
