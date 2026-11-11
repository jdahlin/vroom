int scanf(const char *, ...);
int main(void) {
    int n = 0;
    return scanf("%d", &n) == 1 ? 0 : 1;
}
