// [ext:gnu_attributes] cleanup: the function runs with the variable's
// address when its scope ends: at the end of a block, at break, continue
// and return, in the reverse order of the declarations
extern "C" int printf(const char *, ...);

struct pair {
    int a, b;
};

static void note(int *p)
{
    printf("int %d\n", *p);
}
static void note_pair(pair *p)
{
    printf("pair %d %d\n", p->a, p->b);
}
static void note_pointer(char **p)
{
    printf("pointer %s\n", *p);
}
// a cleanup function taking void *: any pointer converts
static void any(void *p)
{
    printf("any %d\n", *(int *)p);
}
#define SCOPED(f) __attribute__((cleanup(f)))

static int through(int n)
{
    int a SCOPED(note) = n;
    if (n > 1) {
        pair p SCOPED(note_pair) = {n, n + 1};
        return p.a + a;
    }
    return a;
}

int main()
{
    int total = 0;
    for (int i = 0; i < 4; i++) {
        int each SCOPED(note) = i;
        if (i == 1)
            continue;
        if (i == 3)
            break;
        total += each;
    }
    {
        char *s SCOPED(note_pointer) = (char *)"text";
        int x __attribute__((__cleanup__(any))) = 7;
        total += x;
    }
    total += through(1) + through(2);
    return total == 14 ? 0 : 1;
}
