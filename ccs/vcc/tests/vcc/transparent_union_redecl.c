typedef union __attribute__((transparent_union)) {
    int *integer;
    float *floating;
} pointer_arg;
_Static_assert(!__builtin_types_compatible_p(pointer_arg, int *), "object types differ");
_Static_assert(__builtin_types_compatible_p(int(pointer_arg), int(int *)), "first member prototype");
_Static_assert(__builtin_types_compatible_p(int(pointer_arg), int(float *)), "second member prototype");
typedef union __attribute__((transparent_union)) {
    void *generic;
    float *floating;
} other_arg;
_Static_assert(!__builtin_types_compatible_p(int(pointer_arg), int(void *)), "nonmember differs");
_Static_assert(!__builtin_types_compatible_p(int(pointer_arg), int(other_arg)), "different unions differ");
int first(pointer_arg);
int second(pointer_arg);
static int calls(void)
{
    int i = 7;
    float f = 11;
    return first(&i) + second(&f);
}
int first(int *p) { return *p; }
int second(float *p) { return (int)*p; }
int third(int *);
int third(pointer_arg p) { return *p.integer; }
int main(void)
{
    int x = 13;
    return calls() != 18 || third(&x) != 13;
}
