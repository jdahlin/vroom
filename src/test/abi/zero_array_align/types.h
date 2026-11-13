struct __attribute__((aligned(4))) item {
    char a;
    _Bool b;
    float tail[0] __attribute__((packed));
};
struct item item_change(struct item, int, struct item);
int item_varargs(int, ...);
