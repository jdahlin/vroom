int scanf(const char *, ...);
int main(void) {
    int n; char c;
    if (scanf("%d", &n) != 1) return 1;
    if (scanf("%c", &c) != 1) return 1;
    return c == '\n';
}
