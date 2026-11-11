typedef __SIZE_TYPE__ size_t;
void *malloc(size_t); void free(void *);
struct Item { long values[16]; };
int main(void) {
    struct Item *p = malloc(sizeof *p);
    if (!p) return 1;
    p->values[15] = 7;
    free(p);
    return 0;
}
