/* Library calls written as what the front end computes, as clang: alloca, the
 * parts of a complex number, bzero, bcopy, mempcpy, and strlen and strcmp of
 * literals.  Each argument is evaluated once. */
void *alloca(__SIZE_TYPE__);
double creal(_Complex double), cimag(_Complex double);
_Complex double conj(_Complex double);
float crealf(_Complex float), cimagf(_Complex float);
_Complex float conjf(_Complex float);
__SIZE_TYPE__ strlen(const char *);
int strcmp(const char *, const char *);
void bzero(void *, __SIZE_TYPE__);
void bcopy(const void *, void *, __SIZE_TYPE__);
void *mempcpy(void *, const void *, __SIZE_TYPE__);
int printf(const char *, ...);
static int calls;
static __SIZE_TYPE__ three(void) { return ++calls, 3; }
int main(void)
{
    char *a = alloca(32);
    bzero(a, 32);
    bcopy("hello", a, 6);
    bcopy(a, a + 1, 6); /* overlapping: as memmove */
    char *e = mempcpy(a + 8, "xy", three());
    _Complex double z = 1.5 + 2.5i;
    _Complex float f = 3.0f - 0.0fi;
    printf("%s %d %d %d %s\n", a, (int)(e - a), a[31], calls, a + 8);
    printf("%g %g %g %g\n", creal(z), cimag(z), creal(conj(z)), cimag(conj(z)));
    printf("%g %g %g\n", crealf(f), cimagf(f), cimagf(conjf(f)));
    printf("%d %d %d %d\n", (int)strlen("four"), strcmp("a", "b"),
           strcmp("same", "same"), strcmp("b\200", "b\1") > 0);
    return 0;
}
