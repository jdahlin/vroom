void *realloc(void *, __SIZE_TYPE__);
void free(void *);
void *memcpy(void *, const void *, __SIZE_TYPE__);

static void fill(int *left, long *right)
{
    int *xs = 0;
    long *ys = 0;
    unsigned used = 0, limit = 0;
    for (int value = 0; value < 4; ++value) {
        if (used == limit) {
            limit = limit ? limit * 2 : 2;
            xs = (int *)realloc(xs, limit * sizeof *xs);
            ys = (long *)realloc(ys, limit * sizeof *ys);
        }
        xs[used] = value;
        ys[used++] = value + 3;
    }
    if (used) {
        memcpy(left, xs, used * sizeof(int));
        memcpy(right, ys, used * sizeof(long));
    }
    free(xs);
    free(ys);
}

int main(void)
{
    int left[4];
    long right[4];
    fill(left, right);
    for (int i = 0; i < 4; ++i)
        if (left[i] != i || right[i] != i + 3)
            return 1;
    return 0;
}
