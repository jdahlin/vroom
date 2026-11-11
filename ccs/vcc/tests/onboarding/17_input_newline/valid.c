int scanf(const char *, ...);
int main(void) { char c; if (scanf("%c", &c)!=1) return 1; return c == '\n' ? 0 : 1; }
