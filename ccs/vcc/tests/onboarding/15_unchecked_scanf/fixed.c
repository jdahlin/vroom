int scanf(const char *, ...);
int main(void) {
    int n;
    if (scanf("%d", &n) != 1) return 1;
    return n;
}
