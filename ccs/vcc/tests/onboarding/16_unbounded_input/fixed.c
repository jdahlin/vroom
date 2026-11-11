int scanf(const char *, ...);
int main(void) { char name[8]; return scanf("%7s", name) == 1 ? 0 : 1; }
