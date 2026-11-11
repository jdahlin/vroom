typedef __SIZE_TYPE__ size_t;
struct counted { int count; int *data __attribute__((counted_by(count))); };
static __attribute__((noinline)) size_t bound(struct counted *p, int mode)
{
    switch (mode) {
    case 0: return __builtin_dynamic_object_size(p->data, 0);
    case 1: return __builtin_dynamic_object_size(p->data, 1);
    case 2: return __builtin_dynamic_object_size(p->data, 2);
    default: return __builtin_dynamic_object_size(p->data, 3);
    }
}
static __attribute__((noinline)) size_t ordinary(int *p)
{
    return __builtin_dynamic_object_size(p, 1);
}
int main(void)
{
    int data[11]; struct counted p = {11, data};
    for (int mode = 0; mode < 3; mode++)
        if (bound(&p, mode) != 11 * sizeof(int)) return 1;
    if (bound(&p, 3) != 0) return 5;
    p.count = 3;
    if (bound(&p, 1) != 3 * sizeof(int)) return 2;
    p.count = 0;
    if (bound(&p, 1) != 0) return 3;
    if (ordinary(data) != (size_t)-1) return 4;
    return 0;
}
