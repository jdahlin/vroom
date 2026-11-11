int scanf(const char *, ...);
int main(void) { int n=0; int *p=&n; return scanf("%d", p) == 1 ? 0 : 1; }
