/* [ext:gnu_restrict]: __restrict and __restrict__ on pointers (glibc's
   <signal.h>, <string.h>); a restrict-qualified pointer below the top
   level is part of the mangled name (r) */
int copy(int *__restrict to, const int *__restrict__ from, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++) {
        to[i] = from[i];
        s += to[i];
    }
    return s;
}

int first(int *__restrict *pp)
{
    return **pp;
}

struct Buffer {
    char *__restrict data;
    int size;
};

int main()
{
    int a[3] = {1, 2, 3}, b[3];
    int bad = 0;
    if (copy(b, a, 3) != 6 || b[2] != 3) bad |= 1;
    int *__restrict p = a;
    if (first(&p) != 1) bad |= 2;
    char text[4] = "abc";
    Buffer buf = {text, 3};
    if (buf.data[1] != 'b') bad |= 4;
    static_assert(sizeof(Buffer) == 2 * sizeof(void *), "");
    return bad;
}
