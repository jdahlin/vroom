int scanf(const char *, ...);
int main(void) {
    char name[8];
    return scanf("%s", name) == 1 ? 0 : 1;
}
