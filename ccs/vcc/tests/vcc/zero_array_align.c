/* A packed zero-length field still participates in SysV ABI classification. */
struct __attribute__((aligned(4))) item {
    char a;
    _Bool b;
    float tail[0] __attribute__((packed));
};
__attribute__((noinline)) static struct item change(struct item x, int n)
{ x.a += n; x.b = !x.b; return x; }
int main(void)
{
    struct item x = {3, 1};
    x = change(x, 5);
    return x.a != 8 || x.b;
}
