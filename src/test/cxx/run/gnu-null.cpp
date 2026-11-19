/* [ext.gnu] __null: a null pointer constant of type long, a literal that is no
   decl-specifier (libc++'s PTHREAD_MUTEX_INITIALIZER) */
struct pair_of_pointers {
    int *first;
    int *second;
};

static pair_of_pointers initial = {__null, __null};
static_assert(sizeof(__null) == sizeof(void *), "as wide as a pointer");

static int overloaded(int *) { return 1; }
static int overloaded(long) { return 2; }

int main()
{
    int *p = __null;
    if (p || initial.first || initial.second)
        return 1;
    if (overloaded(__null) != 2)
        return 2;
    if (overloaded((int *)__null) != 1)
        return 3;
    return 0;
}
