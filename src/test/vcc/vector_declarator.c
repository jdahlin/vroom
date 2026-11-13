#define V __attribute__((vector_size(16)))
typedef V float vf;
struct record { V float value; };
V float direct(void) { return (V float){ 1, 2, 3, 4 }; }
float call(V float *p) { return (*p)[2]; }
static V float array[2];
static V float (*fp)(void) = direct;
void param(int (__attribute__((__mode__(__SI__))) int));
static int twice(int x) { return x * 2; }
void param(int callback(int))
{
    if (callback(7) != 14) __builtin_abort();
}
int main(void)
{
    struct record r = { fp() };
    array[1] = r.value;
    _Static_assert(sizeof(struct record) == 16, "vector field");
    _Static_assert(sizeof(array) == 32, "array of vectors");
    _Static_assert(__builtin_types_compatible_p(__typeof__(direct), vf(void)), "vector return");
    param(twice);
    return call(&array[1]) != 3 || r.value[3] != 4;
}
